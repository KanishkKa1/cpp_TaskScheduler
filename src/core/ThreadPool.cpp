#include "ThreadPool.hpp"

#include "Worker.hpp"

#include <exception>
#include <iostream>

ThreadPool::ThreadPool(size_t num_threads) : task_queue_(100), worker_states_(num_threads) {
    workers_.reserve(num_threads);

    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(Worker(task_queue_, this, i));
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

// Initiates shutdown
void ThreadPool::shutdown() noexcept {
    if (task_queue_.is_shutdown())
        return;

    task_queue_.shutdown();
}