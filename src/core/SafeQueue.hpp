#pragma once
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

// Thread-safe unbounded queue (MPMC).
//
// Semantics:
// - push/emplace: fail after shutdown
// - pop: blocks until item available or shutdown
// - try_pop: non-blocking
// - shutdown:
//     * prevents further pushes
//     * wakes all waiting threads
//     * allows consumers to drain remaining items
//     * pop returns nullopt when empty after shutdown
//
// Notes:
// - empty() and size() are snapshots and may be stale immediately.
template <typename T> class SafeQueue {
  private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool shutdown_ = false;

  public:
    SafeQueue() = default;

    SafeQueue(const SafeQueue &) = delete;
    SafeQueue &operator=(const SafeQueue &) = delete;

    SafeQueue(SafeQueue &&) = delete;
    SafeQueue &operator=(SafeQueue &&) = delete;

    template <typename U> bool push(U &&value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutdown_) {
                return false;
            }
            queue_.emplace(std::forward<U>(value));
        }
        cv_.notify_one();
        return true;
    }

    template <typename... Args> bool emplace(Args &&...args) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutdown_) {
                return false;
            }
            queue_.emplace(std::forward<Args>(args)...);
        }
        cv_.notify_one();
        return true;
    }

    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty() || shutdown_; });
        if (queue_.empty()) {
            return std::nullopt;
        }

        auto value = std::move(queue_.front());
        queue_.pop();

        return value;
    }

    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (queue_.empty()) {
            return std::nullopt;
        }

        auto value = std::move(queue_.front());
        queue_.pop();

        return value;
    }

    void shutdown() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutdown_) {
                return;
            }
            shutdown_ = true;
        }
        cv_.notify_all();
    }

    bool is_shutdown() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shutdown_;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
};