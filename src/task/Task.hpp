#pragma once

#include <memory>
#include <type_traits>
#include <utility>

// Task is a type-erased callable wrapper.
// - Stores any invocable object
// - Move-only (non-copyable)
// - Used by ThreadPool to execute heterogeneous tasks

class Task {
  private:
    struct ITask {
        virtual void execute() = 0;
        virtual ~ITask() noexcept = default;
    };

    // Concrete implementation of the task
    template <typename F> struct TaskImpl : ITask {
        F func;

        template <typename Fn> explicit TaskImpl(Fn &&f) : func(std::forward<Fn>(f)) {}

        void execute() override {
            func();
        }
    };

    std::unique_ptr<ITask> impl_;

  public:
    Task() = default;

    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;

    Task(Task &&) noexcept = default;
    Task &operator=(Task &&) noexcept = default;

    template <typename F>
        requires(!std::is_same_v<std::decay_t<F>, Task>)
    explicit Task(F &&f) {
        using Decayed = std::decay_t<F>;
        impl_ = std::make_unique<TaskImpl<Decayed>>(std::forward<F>(f));
    }

    // Executes the stored callable.
    void operator()() {
        if (impl_) {
            impl_->execute();
        }
    }

    explicit operator bool() const {
        return static_cast<bool>(impl_);
    }
};