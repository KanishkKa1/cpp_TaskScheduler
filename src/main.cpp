#include "core/ThreadPool.hpp"

#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
int main() {
    std::cout << "Scheduler starting..\n";

    ThreadPool pool(4);
    std::mutex cout_mutex;

    for (int i = 0; i < 10; i++) {
        pool.submit([i, &cout_mutex]() {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "Task " << i << " is running on thread " << std::this_thread::get_id()
                      << "\n";
        });
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "All tasks submitted, shutting down the pool...\n";
    }
    return 0;
}