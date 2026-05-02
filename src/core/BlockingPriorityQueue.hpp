#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <vector>

template <typename T, typename Compare> class BlockingPriorityQueue {
  private:
    std::priority_queue<T, std::vector<T>, Compare> pq_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool shutdown_ = false;

  public:
    BlockingPriorityQueue() = default;

    BlockingPriorityQueue(const BlockingPriorityQueue &) = delete;
    BlockingPriorityQueue &operator=(const BlockingPriorityQueue &) = delete;

    // =========================
    // PUSH
    // =========================
    template <typename U> bool push(U &&value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutdown_)
                return false;
            pq_.push(std::forward<U>(value));
        }
        cv_.notify_one();
        return true;
    }

    // =========================
    // TRY POP (non-blocking)
    // =========================
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (pq_.empty()) {
            return std::nullopt;
        }

        T value = std::move(const_cast<T &>(pq_.top()));
        pq_.pop();
        return value;
    }

    // =========================
    // POP (blocking)
    // =========================
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);

        cv_.wait(lock, [this] { return shutdown_ || !pq_.empty(); });

        if (pq_.empty()) {
            return std::nullopt;
        }

        T value = std::move(const_cast<T &>(pq_.top()));
        pq_.pop();
        return value;
    }

    // =========================
    // SHUTDOWN
    // =========================
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
        }
        cv_.notify_all();
    }

    bool is_shutdown() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shutdown_;
    }

    std::optional<std::reference_wrapper<const T>> peek() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pq_.empty())
            return std::nullopt;

        return std::cref(pq_.top());
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pq_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pq_.size();
    }
};