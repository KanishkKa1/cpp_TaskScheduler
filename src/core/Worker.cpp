#include "Worker.hpp"

#include "ThreadPool.hpp"

#include <thread>

/**
 * Constructor
 */
Worker::Worker(SafeQueue<Task> &queue, ThreadPool *thread_pool, int id)
    : queue_(queue), thread_pool_(thread_pool), id_(id) {}

/**
 * Worker thread main loop.
 *
 * Responsibilities:
 * - Assign thread-local worker_id
 * - Continuously execute tasks via ThreadPool scheduling
 * - Exit when shutdown is signaled AND no work remains
 */
void Worker::operator()() {
    // Set thread-local identity
    worker_id = id_;

    while (true) {
        thread_pool_->help_one_task();

        if (thread_pool_->is_shutdown() && thread_pool_->is_idle()) {
            break;
        }

        std::this_thread::yield();
    }
}