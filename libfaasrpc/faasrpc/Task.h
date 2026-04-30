#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

namespace faabric::rpc {


// Task is the wrapper/promise. It is a generic class as we integrate Faasm
// migration code into the coroutine via the Awaitable `RpcCall` rather than
// `Task`. The protoc plugin exposes `RpcCall` objects to the user rather than
// the `Task`. This is therefore more of an internal tool used alongside the
// plugin.
//
// Based heavily on libcoro::Task<T>. See
// https://github.com/jbaldwin/libcoro/blob/main/include/coro/task.hpp.

template <typename T>
class [[nodiscard]] Task {
  public:
    struct promise_type {
        std::optional<T> value;
        std::exception_ptr exception;
        std::coroutine_handle<> continuation = std::noop_coroutine();

        Task get_return_object()
        {
            return Task{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        auto final_suspend() noexcept
        {
            struct FinalAwaiter {
                bool await_ready() noexcept
                {
                    return false;
                }

                std::coroutine_handle<> await_suspend(
                  std::coroutine_handle<promise_type> h) noexcept
                {
                    return h.promise().continuation;
                }

                void await_resume() noexcept {}
            };

            return FinalAwaiter{};
        }

        void return_value(T v)
        {
            value = std::move(v);
        }

        void unhandled_exception()
        {
            exception = std::current_exception();
        }

        T& result() &
        {
            if (exception) {
                std::rethrow_exception(exception);
            }

            if (!value.has_value()) {
                throw std::runtime_error("Task completed without value");
            }

            return *value;
        }

        T&& result() &&
        {
            if (exception) {
                std::rethrow_exception(exception);
            }

            if (!value.has_value()) {
                throw std::runtime_error("Task completed without value");
            }

            return std::move(*value);
        }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    struct awaitable_base {
        explicit awaitable_base(handle_type h) noexcept
          : coroutine(h)
        {}

        bool await_ready() const noexcept
        {
            return !coroutine || coroutine.done();
        }

        std::coroutine_handle<> await_suspend(
          std::coroutine_handle<> awaitingCoroutine) noexcept
        {
            coroutine.promise().continuation = awaitingCoroutine;
            return coroutine;
        }

        handle_type coroutine = nullptr;
    };

    Task() noexcept = default;

    explicit Task(handle_type h) noexcept
      : handle(h)
    {}

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept
      : handle(std::exchange(other.handle, nullptr))
    {}

    Task& operator=(Task&& other) noexcept
    {
        if (this != &other) {
            destroy();
            handle = std::exchange(other.handle, nullptr);
        }

        return *this;
    }

    ~Task()
    {
        destroy();
    }

    bool is_ready() const noexcept
    {
        return !handle || handle.done();
    }

    bool resume()
    {
        if (handle && !handle.done()) {
            handle.resume();
        }

        return handle && !handle.done();
    }

    bool destroy() noexcept
    {
        if (handle) {
            handle.destroy();
            handle = nullptr;
            return true;
        }

        return false;
    }

    auto operator co_await() & noexcept
    {
        struct awaitable : awaitable_base {
            using awaitable_base::awaitable_base;

            T& await_resume()
            {
                return this->coroutine.promise().result();
            }
        };

        return awaitable{ handle };
    }

    auto operator co_await() const& noexcept
    {
        struct awaitable : awaitable_base {
            using awaitable_base::awaitable_base;

            const T& await_resume()
            {
                auto& promise = this->coroutine.promise();

                if (promise.exception) {
                    std::rethrow_exception(promise.exception);
                }

                if (!promise.value.has_value()) {
                    throw std::runtime_error("Task completed without value");
                }

                return *promise.value;
            }
        };

        return awaitable{ handle };
    }

    auto operator co_await() && noexcept
    {
        struct awaitable : awaitable_base {
            using awaitable_base::awaitable_base;

            T await_resume()
            {
                return std::move(this->coroutine.promise()).result();
            }
        };

        return awaitable{ handle };
    }

    promise_type& promise() &
    {
        return handle.promise();
    }

    const promise_type& promise() const&
    {
        return handle.promise();
    }

    handle_type get_handle() const noexcept
    {
        return handle;
    }

  private:
    handle_type handle = nullptr;
};

} // namespace faabric::rpc