#pragma once
#include <functional>
#include <utility>

class Task {
  private:
    std::function<void()> func_;

  public:
    Task() = default;

    template <typename F> Task(F &&f) : func_(std::forward<F>(f)) {}

    void operator()() {
        if (func_) {
            func_();
        }
    }
};