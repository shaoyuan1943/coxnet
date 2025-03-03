#ifndef COTYPE_H
#define COTYPE_H

#include <utility>
#include <coroutine>
#include <exception>

namespace coxnet {
    class Awaitable {
    public:
        constexpr bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<>) {}
        void await_resume() {}
    };

    template <typename T>
    class Task {
    public:
        class promise_type {
        public:
            promise_type() = default;
            ~promise_type() = default;

            auto initial_suspend() { return std::suspend_never{}; }
            auto final_suspend() noexcept { return std::suspend_never{}; }

            void unhandled_exception() {
                exception_ptr = std::current_exception();
            }

            Task get_return_object() {
                return Task(std::coroutine_handle<promise_type>::from_promise(*this));
            }

            void return_value(T value) {
                result = std::move(value);
            }

            std::exception_ptr exception_ptr;
            T result;
        };

        explicit Task(std::coroutine_handle<promise_type> handle)
            : handle_(handle) {}

        Task(Task&& other) noexcept
            : handle_(std::exchange(other.handle_, {})) {}

        ~Task() {
            if (handle_) {
                handle_.destory();
            }
        }

    private:
        std::coroutine_handle<promise_type> handle_;
    };

    template <>
    class Task<void> {
    public:
        class promise_type {
        public:
            promise_type() = default;
            ~promise_type() = default;

            auto initial_suspend() { return std::suspend_never{}; }
            auto final_suspend() noexcept { return std::suspend_never{}; }

            void unhandled_exception() {
                exception_ptr = std::current_exception();
            }

            Task get_return_object() {
                return Task(std::coroutine_handle<promise_type>::from_promise(*this));
            }

            void return_void() {}

            std::exception_ptr exception_ptr;
        };

        explicit Task(std::coroutine_handle<promise_type> handle)
            : handle_(handle) {}

        Task(Task&& other) noexcept
            : handle_(std::exchange(other.handle_, {})) {}

        ~Task() {
            if (handle_) {
                handle_.destroy();
            }
        }

        void result() const {
            if (handle_.promise().exception_ptr != nullptr) {
                std::rethrow_exception(handle_.promise().exception_ptr);
            }
        }

    private:
        std::coroutine_handle<promise_type> handle_;
    };
}

#endif //COTYPE_H
