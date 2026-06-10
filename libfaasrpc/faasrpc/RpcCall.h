#pragma once

#include <faabric/rpc/rpc.h>
#include <faasrpc/Status.h>
#include <faasrpc/coro_trampoline.h>

#include <coroutine>
#include <cstdio>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace faabric::rpc {

// `RpcCall` is a Faasm-specific awaitable representing one in-flight unary RPC.
//
// Awaiting it waits at a migration-aware suspension point and returns either:
//   - StatusOr<T>{response} on success;
//   - StatusOr<T>{Status{...}} on failure.
//
// RpcCalls are single-use.
template <typename T>
class RpcCall
{
  public:
    explicit RpcCall(uint32_t requestIdIn)
      : requestId(requestIdIn)
    {}

    static RpcCall<T> Failed(Status status)
    {
        RpcCall<T> call(0);
        call.immediateStatus = std::move(status);
        return call;
    }

    RpcCall(const RpcCall&) = delete;
    RpcCall& operator=(const RpcCall&) = delete;

    RpcCall(RpcCall&& other) noexcept
      : requestId(other.requestId)
      , waitStatus(other.waitStatus)
      , immediateStatus(std::move(other.immediateStatus))
    {
        other.requestId = 0;
        other.waitStatus = Rpc_StatusCode::OK;
    }

    RpcCall& operator=(RpcCall&& other) noexcept
    {
        if (this != &other) {
            requestId = other.requestId;
            waitStatus = other.waitStatus;
            immediateStatus = std::move(other.immediateStatus);

            other.requestId = 0;
            other.waitStatus = Rpc_StatusCode::OK;
        }

        return *this;
    }

    bool await_ready() const
    {
        if (immediateStatus.has_value()) {
            return true;
        }

        return __faasm_rpc_test_response(requestId) != 0;
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h)
    {
        if (immediateStatus.has_value()) {
            return h;
        }

        printf("[RpcCall] suspended %u\n", requestId);

        int32_t frameOffset = static_cast<int32_t>(
          reinterpret_cast<uintptr_t>(h.address()));

        waitStatus = __faasm_rpc_wait_migratable(
          requestId,
          faabric::rpc::coro_trampoline_index(),
          frameOffset);

        return h;
    }

    StatusOr<T> await_resume()
    {
        if (immediateStatus.has_value()) {
            return StatusOr<T>{ *immediateStatus };
        }

        if (waitStatus != Rpc_StatusCode::OK) {
            return StatusOr<T>{
                Status{ waitStatus, "RPC wait_migratable failed" }
            };
        }

        printf("[RpcCall] resuming %u\n", requestId);

        while (__faasm_rpc_test_response(requestId) == 0) {
            // Defensive. Normally wait_migratable only returns when ready.
        }

        int32_t respOffset = 0;
        int32_t respLen = 0;
        int32_t errorMsgOffset = 0;
        int32_t errorMsgLen = 0;

        printf("[RpcCall] consuming %u\n", requestId);

        int32_t statusCode = __faasm_rpc_get_response(
          requestId,
          &respOffset,
          &respLen,
          &errorMsgOffset,
          &errorMsgLen);

        if (statusCode != Rpc_StatusCode::OK) {
            std::string errorMessage = "RPC get_response failed";

            if (errorMsgOffset != 0 && errorMsgLen > 0) {
                errorMessage.assign(
                  reinterpret_cast<const char*>(errorMsgOffset),
                  static_cast<size_t>(errorMsgLen));
            }

            printf("[RpcCall] get_response failed status=%d msg='%s'\n",
                   statusCode,
                   errorMessage.c_str());

            return StatusOr<T>{ Status{ statusCode, std::move(errorMessage) } };
        }

        T resp;
        if (!resp.ParseFromArray(
              reinterpret_cast<const void*>(respOffset), respLen)) {
            return StatusOr<T>{
                Status{ Rpc_StatusCode::INTERNAL,
                        "RPC response deserialisation failed" }
            };
        }

        return StatusOr<T>{ std::move(resp) };
    }

  private:
    uint32_t requestId = 0;
    int32_t waitStatus = Rpc_StatusCode::OK;
    std::optional<Status> immediateStatus;
};

} // namespace faabric::rpc