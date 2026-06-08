#pragma once

#include <faabric/rpc/rpc.h>
#include <faasrpc/coro_trampoline.h>

#include <coroutine>
#include <string>

namespace faabric::rpc {

struct IncomingRequest {
    uint32_t requestId;
    std::string method;
    std::string payload;
    std::string replyHost;
    int32_t replyPort;
};

class RpcReceive
{
  public:
    bool await_ready() const noexcept
    {
        return false;
    }

    bool await_suspend(std::coroutine_handle<> h)
    {
        handle_ = h.address();
        fetch(h);

        // In the non-migration case, fetch has completed synchronously, so
        // resume the coroutine immediately and let await_resume consume result_.
        return false;
    }

    std::optional<IncomingRequest> await_resume()
    {
        // If we resume after migration, await_suspend's fetch did not complete.
        // The awaiter object was restored as part of the coroutine frame, with
        // fetched_ still false, so re-poll the now-local queue.
        if (!fetched_) {
            auto h = std::coroutine_handle<>::from_address(handle_);
            fetch(h);
        }

        if (status_ != Rpc_StatusCode::OK) {
            return std::nullopt;
        }

        return std::move(result_);
    }

  private:
    static std::string copyGuestString(int32_t offset, int32_t len)
    {
        if (len <= 0) {
            return {};
        }

        return std::string(
          reinterpret_cast<const char*>(static_cast<uintptr_t>(offset)),
          static_cast<size_t>(len));
    }

    void fetch(std::coroutine_handle<> h)
    {
        fetched_ = false;

        int32_t frameOffset = static_cast<int32_t>(
          reinterpret_cast<uintptr_t>(h.address()));

        int32_t methodOffset = 0;
        int32_t methodLen = 0;
        int32_t payloadOffset = 0;
        int32_t payloadLen = 0;
        int32_t replyHostOffset = 0;
        int32_t replyHostLen = 0;

        status_ = __faasm_rpc_get_request(
          faabric::rpc::coro_trampoline_index(),
          frameOffset,
          &result_.requestId,
          &methodOffset,
          &methodLen,
          &payloadOffset,
          &payloadLen,
          &replyHostOffset,
          &replyHostLen,
          &result_.replyPort);

        if (status_ == Rpc_StatusCode::OK) {
            result_.method = copyGuestString(methodOffset, methodLen);
            result_.payload = copyGuestString(payloadOffset, payloadLen);
            result_.replyHost = copyGuestString(replyHostOffset, replyHostLen);
        } else {
            result_ = {};
        }

        fetched_ = true;
    }

    IncomingRequest result_{};
    int32_t status_ = Rpc_StatusCode::OK;
    bool fetched_ = false;
    void* handle_ = nullptr;
};

} // namespace faabric::rpc