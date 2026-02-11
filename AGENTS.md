# Agent Guidelines

## Build Commands

```bash
# Build all targets
xmake build

# Build specific target
xmake build backend
xmake build main
xmake build tests

# Run all tests
xmake test

# Run specific test file (filter by name)
xmake run tests -t "test_name"

# Run main executable
xmake run main

# Format code
clang-format -i --style=file <file>
```

## Code Style

### Imports
- Use `#pragma once` for include guards
- Sort includes: std library headers first (sorted by std.hpp regex), then local headers
- Use `#include <...>` for all includes (not `"..."`)
- No duplicate imports
- Merge related includes together

### Formatting
- Indent: 4 spaces, no tabs
- Line limit: 80 characters
- Pointer/reference alignment: `T*` and `T&` (right-aligned)
- Always use braces for control flow
- Place requires clauses on their own line
- Empty line before access modifiers (logical block)
- No empty line after access modifiers
- Brace wrapping: no braces on new line (LLVM style)
- Namespace indentation: none

### Types
- Use `template <class T>` or `template <typename T>` consistently (prefer `class`)
- Use `std::remove_cvref_t<T>` for template argument cleanup
- Use concepts for type constraints with `requires` clauses
- Use `[[nodiscard]]` on types and functions where return values must be used
- Prefer `Task<T>` over raw coroutine handles
- Use `std::variant` and `std::optional` for type-safe optional values

### Naming Conventions
- Types (structs, classes, enums, concepts): `PascalCase` (e.g., `Task`, `io_context`)
- Functions and methods: `snake_case` (e.g., `when_all`, `get_handle`)
- Variables and members: `snake_case` (e.g., `worker_`, `parent_coro`)
- Private member variables: trailing underscore `_` (e.g., `_value`, `_mtx`)
- Template type parameters: `PascalCase` (e.g., `TaskT`, `PromiseType`)
- Constants: `ALL_CAPS` or `snake_case` (be consistent within file)
- Enum values: `PascalCase` or `SCREAMING_SNAKE_CASE` (be consistent)

### Error Handling
- Exception-based error propagation
- Store exceptions in promise objects, rethrow on result access
- Use `try { } catch (...)` blocks with `std::current_exception()`
- Exceptions from detached void tasks are swallowed to prevent hangs
- Use `REQUIRE`, `REQUIRE_THROWS_AS` in tests (Catch2)
- First exception wins in combinators (when_all, when_any)

### Comments
- Use docgen-style documentation with `///` for public APIs:
  - `@brief` - brief description
  - `@tparam` - template parameter documentation
  - `@param` - parameter documentation
  - `@return` - return value documentation
- Bilingual comments: Chinese allowed in core async files (when_*.hpp)
- Use descriptive Chinese comments in Chinese-speaking code sections
- Keep comments concise and focused on "why" not "what"
- Use `TODO:` for future work

### Coroutines
- Always mark coroutine functions with `Task<T>` return type
- Use `co_return` to return values from coroutines
- Use `co_await` to await other coroutines
- Use `co_yield` for generators
- Coroutines are move-only (no copying)
- Detach fire-and-forget tasks with `.detach()`
- Use `when_ready()` to wait for completion without result

### Concurrency
- Use `std::atomic` for simple state flags/counters
- Use `std::mutex` with `std::scoped_lock` for complex state
- Use `std::memory_order_*` explicitly in atomic operations:
  - `memory_order_relaxed` for standalone counters
  - `memory_order_acquire` for loads
  - `memory_order_release` for stores
  - `memory_order_acq_rel` for combined operations
- Thread-local storage: `thread_local` for context management
- Prefer move semantics for ownership transfer

### Logging
- Use the `LOG` singleton: `LOG::i()`, `LOG::d()`, `LOG::w()`, `LOG::e()`
- Use `std::format_string<Args...>` for type-safe formatting
- Include `std::source_location::current()` for source tracking
- Log levels: `DEBUG`, `INFO`, `WARN`, `ERROR`
- Minimum log level: `LogLevel::DEBUG` (change in LOG class if needed)

### Testing
- Use Catch2: `TEST_CASE("test_name", "[tag]")`
- Place tests in `tests/` directory matching source structure
- Test both success and failure paths
- Test exception handling with `REQUIRE_THROWS_AS`
- Test move semantics where applicable
- Manual coroutine resumption in tests (no auto-scheduler)
- Example test file: `tests/async/test_task.cpp`

### General Guidelines
- Prefer `constexpr` where possible
- Use `noexcept` on functions that don't throw
- Use `[[nodiscard]]` on functions whose return values must be checked
- Use `std::move()` to transfer ownership explicitly
- Prefer `std::format` and `std::print` over iostreams
- Use `std::unique_ptr` and `std::shared_ptr` for smart pointers
- Use RAII for resource management
- Delete copy constructors/operators for move-only types
- Use `std::exchange` for atomic or thread-safe updates
