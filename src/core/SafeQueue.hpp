#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

template <typename T> class SafeQueue {
  private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool shutdown_ = false; // Indicates no more work will be added to the queue

  public:
    // push a new task in the queue and notify one waiting thread
    void push(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // If shutdown signal is received, do not add new work to the queue
            if (shutdown_) {
                throw std::runtime_error("Cannot push to queue after shutdown signal is received.");
                //  return;
            }
            // move value to avoid unnecessary copies
            queue_.push(std::move(value));
        }
        // notify one waiting thread that new work is available in the queue
        cv_.notify_one();
    }

    // blocking pop method that waits until work is available in the queue or
    // shutdown signal is received
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);

        // wait untill either
        // 1. work is available in the queue
        // 2. System shutdown signal is received
        cv_.wait(lock, [this] { return !queue_.empty() || shutdown_; });

        // if shutdown signal is received and there is no work in the queue, return nullopt
        if (shutdown_ && queue_.empty()) {
            return std::nullopt;
        }

        // Safe to access queue as we are holding the lock and there is work available
        T value = std::move(queue_.front());
        queue_.pop();

        return value;
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Set shutdown flag to true -> no more work will be added
            shutdown_ = true;
        }
        // Notify all waiting threads to wake up and check the shutdown flag
        cv_.notify_all();
    }
};