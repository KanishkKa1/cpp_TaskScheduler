#include "core/ThreadPool.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

int main() {
    std::cout << "Scheduler starting..\n";

    ThreadPool pool(4);
    std::vector<std::future<int>> futures;

    std::mutex cout_mutex;
    std::atomic<bool> running{true};

    // Thread to monitor pool status
    std::thread monitor([&]() {
        while (running) {
            {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << "Active Workers: " << pool.active_workers()
                          << ", Pending Tasks: " << pool.pending_task()
                          << ", Total Submitted: " << pool.total_submitted()
                          << ", Total Completed: " << pool.total_completed() << "\n";
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    // Submit tasks
    for (int i = 0; i < 10; i++) {
        futures.push_back(pool.submit([i, &cout_mutex]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * (i % 3)));

            {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << "Task " << i << " is running on thread " << std::this_thread::get_id()
                          << "\n";
            }

            return i * i;
        }));
    }

    for (int i = 0; i < futures.size(); ++i) {
        try {
            int result = futures[i].get();

            {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << "Task " << i << " -> Result: " << result << "\n";
            }

        } catch (const std::exception &e) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "Task " << i << " -> Error: " << e.what() << "\n";
        }
    }

    // stop monitor thread
    running = false;
    monitor.join();

    pool.shutdown();

    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "All tasks completed. Shutting down the pool...\n";
        std::cout << "\nFinal Metrics:\n";
        std::cout << "Submitted: " << pool.total_submitted() << "\n";
        std::cout << "Completed: " << pool.total_completed() << "\n";
        std::cout << "Pending: " << pool.pending_task() << "\n";
        std::cout << "Active: " << pool.active_workers() << "\n";
    }

    return 0;
}