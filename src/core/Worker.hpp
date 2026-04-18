#pragma once

#include "ThreadPool.hpp"
#include "core/SafeQueue.hpp"
#include "task/Task.hpp"

#include <iostream>

class ThreadPool; // Forward declaration to avoid circular dependency

class Worker {
  private:
    SafeQueue<Task> &queue_;
    ThreadPool *thread_pool_;

  public:
    explicit Worker(SafeQueue<Task> &queue, ThreadPool *thread_pool)
        : queue_(queue), thread_pool_(thread_pool) {}

    void operator()() {
        while (true) {
            auto task = queue_.pop();
            if (!task) {
                break; // shutdown signaled and queue is empty
            }
            thread_pool_->on_task_start();
            try {
                (*task)();
            } catch (const std::exception &e) {
                // Prevent worker thread termination due to task exceptions.
                std::cerr << "Task threw an exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Task threw an unknown exception." << std::endl;
            }
            thread_pool_->on_task_end();
        }
    }
};