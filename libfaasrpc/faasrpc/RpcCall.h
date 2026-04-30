#pragma once

#include <rpc.h>
#include <faasrpc/coro_trampoline.h>

#include <coroutine>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>


namespace faabric::coro {

// `RpcCall` is a domain-specific `Awaitable` which encapsulates Faasm's RPC and
// migration semantics. `RpcCall` implements the coroutine await interface,
// integrating directly with the coroutine model, providing natural suspension
// points that are aware of Faasm's runtime behaviour.
//
// `RpcCall` coordinates waiting for RPC completion with migration, allowing
// suspended coroutines to be safely snapshotted and restored on another host
// without exposing these runtime details to the user.
//
// Use the unary `co_await` on `RpcCall<T>` objects.
// RpcCalls are single-use.
template <typename T>
class RpcCall {
  public:
    explicit RpcCall(int32_t requestId)
      : requestId(requestId)
      , consumed(false)
    {}

    // Not copyable — owns the requestId
    RpcCall(const RpcCall&) = delete;
    RpcCall& operator=(const RpcCall&) = delete;
    RpcCall(RpcCall&&) = default;
    RpcCall& operator=(RpcCall&&) = default;

    bool await_ready() const
    {
        if (consumed) {
            throw std::runtime_error("RpcCall awaited more than once");
        }

        return __faasm_rpc_test_response(requestId) != 0;
    }

    bool await_suspend(std::coroutine_handle<> h) noexcept
    {
        // If migration happens inside wait_migratable, the frame at this
        // offset is snapshotted and resumed on the new host. The new host
        // reconstructs the handle and calls
        // resume(), continuing from await_resume() below.
        int32_t frameOffset = static_cast<int32_t>(
            reinterpret_cast<uintptr_t>(h.address()));

        __faasm_rpc_wait_migratable(
            requestId,
            faabric::coro::coro_trampoline_index(),
            frameOffset);

        // Resume and continue execution by returning false.
        return false;
    }

    T await_resume()
    {
        int32_t respOffset = 0;
        int32_t respLen = 0;
        int32_t status =
            __faasm_rpc_get_response(requestId, &respOffset, &respLen);

        if (status != 0) {
            throw std::runtime_error(
                "RpcCall: get_response failed with status "
                + std::to_string(status));
        }

        T resp;
        if (!resp.ParseFromArray(
                reinterpret_cast<const void*>(respOffset), respLen)) {
            throw std::runtime_error("RpcCall: failed to parse response");
        }
        return resp;
    }

  private:
    int32_t requestId;
    bool consumed;
};

}   // namespace faabric::coro