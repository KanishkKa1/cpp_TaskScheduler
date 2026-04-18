#pragma once

#include "ThreadPool.hpp"
#include "core/SafeQueue.hpp"
#include "task/Task.hpp"

#include <iostream>

inline thread_local int worker_id = -1;

class ThreadPool; // Forward declaration to avoid circular dependency

class Worker {
  private:
    SafeQueue<Task> &queue_;
    ThreadPool *thread_pool_;
    int id_; // Thread Id

  public:
    explicit Worker(SafeQueue<Task> &queue, ThreadPool *thread_pool, int id);

    void operator()();
};