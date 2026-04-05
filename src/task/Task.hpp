#pragma once
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

class Task {
  private:
    // Type-erased interface
    struct ITask {
        virtual void execute() = 0;
        virtual ~ITask() = default;
    };

    // Concrete implementation of the task
    template <typename F> struct TaskImpl : ITask {
        F func;
        template <typename fn> explicit TaskImpl(fn &&f) : func(std::forward<fn>(f)) {}
        void execute() override {
            func();
        }
    };

    // storage
    std::unique_ptr<ITask> impl_;

  public:
    // constructores
    Task() = default;

    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;

    Task(Task &&) noexcept = default;
    Task &operator=(Task &&) noexcept = default;

    // template constructor
    template <typename F> Task(F &&f) {
        using Decayed = std::decay_t<F>;
        impl_ = std::make_unique<TaskImpl<Decayed>>(std::forward<F>(f));
    }

    // execute the task
    void operator()() {
        if (impl_) {
            impl_->execute();
        }
    }

    // validity check
    explicit operator bool() const {
        return static_cast<bool>(impl_);
    }
};