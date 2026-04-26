#include "Worker.hpp"

#include "ThreadPool.hpp"

#include <chrono>
#include <thread>

// Thread-local worker id definition
thread_local int worker_id = -1;

// =======================
// Constructor
// =======================
Worker::Worker(ThreadPool &thread_pool, int id) : thread_pool_(thread_pool), id_(id) {}

// =======================
// Worker execution loop
// =======================
void Worker::operator()() {
    // Assign thread-local identity
    worker_id = id_;

    while (true) {
        // Execute one unit of work
        thread_pool_.help_one_task();

        // Exit condition
        if (thread_pool_.is_shutdown() && thread_pool_.is_idle()) {
            break;
        }

        // Controlled backoff (prevents CPU spinning)
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    // Reset thread-local state
    worker_id = -1;
}