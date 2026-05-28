#pragma once

#include <faabric/rpc/rpc.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace faabric::rpc {

class Status
{
  public:
    Status() noexcept
      : code_(Rpc_StatusCode::OK)
    {}

    Status(int32_t code, std::string message = {})
      : code_(code)
      , message_(std::move(message))
    {}

    static Status OK()
    {
        return Status{ Rpc_StatusCode::OK, "" };
    }

    bool ok() const noexcept
    {
        return code_ == Rpc_StatusCode::OK;
    }

    int32_t code() const noexcept
    {
        return code_;
    }

    std::string_view message() const noexcept
    {
        return message_;
    }

  private:
    int32_t code_;
    std::string message_;
};

template <typename T>
class StatusOr
{
  public:
    StatusOr(Status status)
      : status_(std::move(status))
    {
        if (status_.ok()) {
            throw std::invalid_argument(
              "StatusOr constructed with OK status and no value");
        }
    }

    StatusOr(T value)
      : status_(Status::OK())
      , value_(std::move(value))
    {}

    bool ok() const noexcept
    {
        return status_.ok();
    }

    const Status& status() const noexcept
    {
        return status_;
    }

    const T& value() const&
    {
        if (!ok()) {
            throw std::runtime_error("Accessing value of non-OK StatusOr");
        }

        return *value_;
    }

    T& value() &
    {
        if (!ok()) {
            throw std::runtime_error("Accessing value of non-OK StatusOr");
        }

        return *value_;
    }

    T&& value() &&
    {
        if (!ok()) {
            throw std::runtime_error("Accessing value of non-OK StatusOr");
        }

        return std::move(*value_);
    }

  private:
    Status status_;
    std::optional<T> value_;
};

} // namespace faabric::rpc