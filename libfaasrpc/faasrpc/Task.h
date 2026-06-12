#pragma once

#include <atomic>
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

namespace faabric::rpc {

template <typename T>
class Task;

namespace detail {

class TaskPromiseBase
{
    friend struct FinalAwaiter;

    struct FinalAwaiter {
        bool await_ready() const noexcept
        {
            return false;
        }

        template <typename Promise>
        void await_suspend(std::coroutine_handle<Promise> h) noexcept
        {
            auto& promise = h.promise();

            if (promise.state.exchange(true, std::memory_order_acq_rel)) {
                auto continuation = std::exchange(
                    promise.continuation,
                    std::noop_coroutine());

                continuation.resume();
            }
        }

        void await_resume() noexcept {}
    };

  public:
    TaskPromiseBase() noexcept = default;

    std::suspend_always initial_suspend() noexcept
    {
        return {};
    }

    FinalAwaiter final_suspend() noexcept
    {
        return {};
    }

    bool trySetContinuation(std::coroutine_handle<> awaitingCoroutine) noexcept
    {
        continuation = awaitingCoroutine;
        return !state.exchange(true, std::memory_order_acq_rel);
    }

  private:
    template <typename>
    friend class TaskPromise;

    std::coroutine_handle<> continuation = std::noop_coroutine();
    std::atomic<bool> state = false;
};

template <typename T>
class TaskPromise final : public TaskPromiseBase
{
  public:
    Task<T> get_return_object() noexcept;

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

    const T& result() const&
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

  private:
    std::optional<T> value;
    std::exception_ptr exception;
};

template <>
class TaskPromise<void> final : public TaskPromiseBase
{
  public:
    Task<void> get_return_object() noexcept;

    void return_void() noexcept {}

    void unhandled_exception()
    {
        exception = std::current_exception();
    }

    void result()
    {
        if (exception) {
            std::rethrow_exception(exception);
        }
    }

  private:
    std::exception_ptr exception;
};

} // namespace detail

template <typename T>
class [[nodiscard]] Task {
  public:
    using promise_type = detail::TaskPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    struct awaitable_base {
        explicit awaitable_base(handle_type h) noexcept
          : coroutine(h)
        {}

        bool await_ready() const noexcept
        {
            return !coroutine || coroutine.done();
        }

        bool await_suspend(std::coroutine_handle<> awaitingCoroutine) noexcept
        {
            // cppcoro-style non-symmetric-transfer path:
            //
            // Start the child first. If it completes synchronously, final_suspend()
            // marks the task ready but does not resume the parent. We then observe
            // that state and return false, so the parent continues after this
            // resume call has unwound.
            //
            // If the child suspends, trySetContinuation() records the parent and
            // returns true, so the parent suspends and will be resumed later.
            coroutine.resume();
            return coroutine.promise().trySetContinuation(awaitingCoroutine);
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
                return this->coroutine.promise().result();
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
                auto h = std::exchange(this->coroutine, nullptr);

                try {
                    T result = std::move(h.promise()).result();
                    h.destroy();
                    return result;
                } catch (...) {
                    h.destroy();
                    throw;
                }
            }
        };

        return awaitable{ std::exchange(handle, nullptr) };
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

template <>
class [[nodiscard]] Task<void> {
  public:
    using promise_type = detail::TaskPromise<void>;
    using handle_type = std::coroutine_handle<promise_type>;

    struct awaitable_base {
        explicit awaitable_base(handle_type h) noexcept
          : coroutine(h)
        {}

        bool await_ready() const noexcept
        {
            return !coroutine || coroutine.done();
        }

        bool await_suspend(std::coroutine_handle<> awaitingCoroutine) noexcept
        {
            coroutine.resume();
            return coroutine.promise().trySetContinuation(awaitingCoroutine);
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

            void await_resume()
            {
                this->coroutine.promise().result();
            }
        };

        return awaitable{ handle };
    }

    auto operator co_await() const& noexcept
    {
        struct awaitable : awaitable_base {
            using awaitable_base::awaitable_base;

            void await_resume()
            {
                this->coroutine.promise().result();
            }
        };

        return awaitable{ handle };
    }

    auto operator co_await() && noexcept
    {
        struct awaitable : awaitable_base {
            using awaitable_base::awaitable_base;

            void await_resume()
            {
                auto h = std::exchange(this->coroutine, nullptr);

                try {
                    h.promise().result();
                    h.destroy();
                } catch (...) {
                    h.destroy();
                    throw;
                }
            }
        };

        return awaitable{ std::exchange(handle, nullptr) };
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

namespace detail {

template <typename T>
Task<T> TaskPromise<T>::get_return_object() noexcept
{
    return Task<T>{
        std::coroutine_handle<TaskPromise<T>>::from_promise(*this)
    };
}

inline Task<void> TaskPromise<void>::get_return_object() noexcept
{
    return Task<void>{
        std::coroutine_handle<TaskPromise<void>>::from_promise(*this)
    };
}

} // namespace detail

} // namespace faabric::rpc