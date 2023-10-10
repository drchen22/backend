#pragma once

#include <condition_variable>
#include <ctime>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <semaphore>
#include <thread>
#include <vector>
#include "mutex.h"

class Thread {
public:
  Thread(std::function<void()> func);
  ~Thread();

  void join();

private:
  static void *run(void *arg);

private:
  pid_t m_id = -1;

  pthread_t m_thread = 0;

  std::function<void()> m_func;
  Semaphore m_semaphore;
};
