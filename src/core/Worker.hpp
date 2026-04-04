#pragma once
#include "core/SafeQueue.hpp"
#include "task/Task.hpp"

#include <functional>
#include <iostream>
#include <mutex>

class Worker {
  private:
    SafeQueue<Task> &queue_;

  public:
    explicit Worker(SafeQueue<Task> &queue) : queue_(queue) {}
    void operator()() {
        while (true) {
            auto task = queue_.pop();
            if (!task.has_value()) {
                break; // shutdown signaled and queue is empty
            }
            try {
                (*task)();
            } catch (const std::exception &e) {
                // Prevent worker thread termination due to task exceptions.
                std::cerr << "Task threw an exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Task threw an unknown exception." << std::endl;
            }
        }
    }
};