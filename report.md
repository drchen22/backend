# resume_on 间歇性挂起 Bug 修复报告

## 概述

在实现 `resume_on` 协程原语（用于跨 `io_context` 线程调度）的并发测试时，发现屏障测试 `multiple coroutines migrate concurrently` 存在约 5-8% 的间歇性挂起概率。根本原因涉及两个独立 bug：测试逻辑错误与框架层 eventfd 唤醒竞争。

## 复现条件

- 测试用例：`resume_on: multiple coroutines migrate concurrently`
- 3 个协程从 `ctx[0]` 迁移到 `ctx[2]`，通过原子屏障同步后迁回 `ctx[0]`
- 挂起概率：~5-8%（约每 20 次运行出现 1 次）
- 表现：测试进程无限挂起，需要 SIGTERM 终止

## Bug 1：测试逻辑 — 屏障计数器复用

### 问题

原始测试使用单一 `barrier` 原子变量同时充当到达计数和完成计数：

```cpp
// 旧代码（有 bug）
barrier.fetch_add(1);                          // 到达同步
while (barrier.load() < count) { co_await yield(); }
// ... 迁回 ctx[0] ...
barrier.fetch_sub(1);                          // 完成递减 ← 问题所在
```

快协程完成迁移并执行 `fetch_sub(1)` 后，`barrier` 值从 3 降到 2。此时仍在 `while` 循环中检查的其他协程看到 `barrier < count`，陷入无限 yield 循环，永远无法通过屏障。

### 修复

将到达计数和完成计数分离为两个独立原子变量：

```cpp
// 新代码（已修复）
std::atomic<int> arrived{0};  // 仅 fetch_add，永不递减
std::atomic<int> done{0};     // 独立完成计数

arrived.fetch_add(1, std::memory_order_acq_rel);
while (arrived.load(std::memory_order_acquire) < count) {
    co_await ctx[2].yield();
}
// ... 迁回 ctx[0] ...
if (done.fetch_add(1, std::memory_order_acq_rel) == count - 1) {
    // 最后一个完成的协程负责停止所有 context
    for (int i = 0; i < 4; ++i) ctx[i].stop();
}
```

**文件**：`tests/async/test_resume_on.cpp:103-148`

## Bug 2：框架层 — eventfd 提前消费导致唤醒丢失

### 问题

原始的跨线程唤醒机制使用 NOP SQE（从任意线程向 io_uring SQ 环提交空操作）来唤醒目标线程的 `submit_and_wait()`。这种方式存在 io_uring SQ 环的线程安全竞争：非 owner 线程操作 SQ 环可能导致数据损坏。

修复方案是将唤醒机制迁移到 eventfd + POLL_ADD：
- 跨线程写入 eventfd（线程安全的 `::write()` 系统调用）
- owner 线程通过 `POLL_ADD` SQE 监听 eventfd

但在迁移过程中引入了一个新 bug：`do_worker_part()` 在函数开头调用了 `drain_eventfd()`，提前消费了 eventfd 中的唤醒信号。随后 `arm_eventfd()` + `submit_and_wait()` 注册新的 poll，但信号已被消费 → `submit_and_wait` 永久阻塞 → `can_stop()` 永远不被重新检查 → 线程挂起。

```
时序：
  其他线程 → write(eventfd_)     // 写入唤醒信号
  owner线程 → drain_eventfd()    // 提前消费了信号！
  owner线程 → arm_eventfd()      // 注册 POLL_ADD
  owner线程 → submit_and_wait(1) // 永久阻塞，没有信号可等
```

### 修复

1. 移除 `do_worker_part()` 开头的 `drain_eventfd()` 调用
2. eventfd 仅在 `submit_and_wait()` 返回后 drain（此时 poll 已完成，信号被正确消费）

修复后的 `do_worker_part()` 流程：

```cpp
void do_worker_part() {
    drain_spawn_queue();                          // 1. 排空跨线程 spawn 队列

    std::size_t budget = config::swap_capacity;
    while (meta_.has_task_ready() && budget-- > 0) {  // 2. 执行就绪协程
        auto h = meta_.schedule();
        if (h && !h.done()) h.resume();
    }

    drain_spawn_queue();                          // 3. 再次排空（执行期间可能有新 spawn）

    if (!meta_.has_task_ready() &&
        !has_pending_spawn_.load(std::memory_order_acquire)) {
        meta_.arm_eventfd(eventfd_);              // 4. 注册 eventfd poll
        meta_.submit_and_wait(1);                 // 5. 阻塞等待（eventfd 或 IO 完成）
        drain_eventfd();                          // 6. 排空 eventfd（仅在 poll 完成后）
    }
}
```

**文件**：`server/include/async/io_context.hpp:273-292`

## 关联变更：NOP → eventfd 唤醒机制迁移

| 方面 | 旧方案（NOP SQE） | 新方案（eventfd） |
|------|-------------------|--------------------|
| 唤醒方式 | 从任意线程提交 NOP SQE | 从任意线程 `::write(eventfd_)` |
| 线程安全 | 不安全（非 owner 操作 SQ 环） | 安全（`::write()` 是线程安全的系统调用） |
| 监听方式 | NOP 完成 CQE 唤醒 | `POLL_ADD` SQE 监听 eventfd 可读事件 |
| 注册方 | 跨线程（任意线程） | owner 线程（`arm_eventfd()`） |

### `worker_meta` 变更

```cpp
// 旧：notify_external() — 从任意线程提交 NOP SQE（不安全）
void notify_external() {
    auto *sqe = io_uring_get_sqe(&ring_);
    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data(sqe, nullptr);
    io_uring_submit(&ring_);
}

// 新：arm_eventfd() — 仅由 owner 线程调用，注册 POLL_ADD
void arm_eventfd(int efd) noexcept {
    auto *sqe = io_uring_get_sqe(&ring_);
    if (sqe) {
        io_uring_prep_poll_add(sqe, efd, POLLIN);
        io_uring_sqe_set_data(sqe, nullptr);
    }
}
```

**文件**：`server/include/async/details/worker_meta.hpp:69-75`

### `io_context` 新增成员

```cpp
int eventfd_{-1};                                          // eventfd 文件描述符
std::atomic<bool> has_pending_spawn_{false};                // 跨线程 spawn 待处理标志

void notify() noexcept {                                   // 线程安全的唤醒
    uint64_t val = 1;
    ::write(eventfd_, &val, sizeof(val));
}

void drain_eventfd() noexcept {                            // 排空 eventfd
    uint64_t val;
    while (::read(eventfd_, &val, sizeof(val)) > 0) {}
}
```

**文件**：`server/include/async/io_context.hpp:268-271`, `303-306`, `313-314`, `318`

### `can_stop()` 增强

```cpp
bool can_stop() noexcept {
    return will_stop_.load(std::memory_order_acquire) &&
           work_.load(std::memory_order_acquire) == 0 &&
           !meta_.has_task_ready() &&
           !has_pending_spawn_.load(std::memory_order_acquire);  // 新增检查
}
```

**文件**：`server/include/async/io_context.hpp:196-201`

## 修改文件清单

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `server/include/async/io_context.hpp` | 修改 | eventfd 生命周期管理、`notify()`、`drain_eventfd()`、`arm_eventfd()` 集成、`has_pending_spawn_` 原子、`do_worker_part()` 重构、`can_stop()` 增强 |
| `server/include/async/details/worker_meta.hpp` | 修改 | `notify_external()` → `arm_eventfd()`，移除不安全的跨线程 SQE 提交 |
| `tests/async/test_resume_on.cpp` | 修改 | 屏障测试分离 `arrived`/`done` 计数器，移除调试输出 |

## 验证结果

- 屏障测试连续 **500 次运行全部通过**（修复前 ~5-8% 失败率）
- 完整测试套件：**82 测试 / 211 断言，全部通过**
- 无回归
