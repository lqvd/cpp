#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace faabric::rpc {

// The coroutine Promise type.

template <typename T>
class Task {
  public:
    struct promise_type {
        std::optional<T> value;
        std::exception_ptr exception;
        std::coroutine_handle<> continuation;

        Task get_return_object() {
            return Task{ std::coroutine_handle<promise_type>::from_promise(*this) };
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        // On finalisation, resume our awaiter (the outer coroutine).
        auto final_suspend() noexcept {
            struct FinalAwaiter {
                bool await_ready() noexcept { return false; }
                std::coroutine_handle<> await_suspend(
                  std::coroutine_handle<promise_type> h) noexcept
                {
                    auto& promise = h.promise();
                    return promise.continuation
                        ? promise.continuation
                        : std::noop_coroutine();
                }
                void await_resume() noexcept {}
            };
            return FinalAwaiter{};
        }

        void return_value(T v) { value = std::move(v); }
        void unhandled_exception() { exception = std::current_exception(); }
    };

    explicit Task(std::coroutine_handle<promise_type> h) : handle(h) {}

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept
      : handle(std::exchange(other.handle, {})) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = std::exchange(other.handle, {});
        }
        return *this;
    }

    ~Task() {
        if (handle) handle.destroy();
    }

    // co_await on a Task chains coroutines: start the inner task, suspend
    // the outer, resume the outer when the inner returns.
    auto operator co_await() noexcept {
        struct Awaiter {
            std::coroutine_handle<promise_type> inner;

            bool await_ready() const noexcept { return inner.done(); }

            std::coroutine_handle<> await_suspend(
              std::coroutine_handle<> outer) noexcept
            {
                inner.promise().continuation = outer;
                return inner; // symmetric transfer: jump straight to inner
            }

            T await_resume() {
                if (inner.promise().exception) {
                    std::rethrow_exception(inner.promise().exception);
                }
                return std::move(*inner.promise().value);
            }
        };
        return Awaiter{ handle };
    }

  private:
    std::coroutine_handle<promise_type> handle;
};

} // namespace faabric::rpc