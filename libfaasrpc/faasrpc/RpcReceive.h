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

class RpcReceive {
  public:
    bool await_ready() const noexcept { return false; }

    bool await_suspend(std::coroutine_handle<> h) noexcept {
        int32_t frameOffset = static_cast<int32_t>(
            reinterpret_cast<uintptr_t>(h.address()));

        int32_t methodOffset, methodLen;
        int32_t payloadOffset, payloadLen;
        int32_t replyHostOffset, replyHostLen;

        status_ = __faasm_rpc_get_request(
            faabric::rpc::coro_trampoline_index(),
            frameOffset,
            &result_.requestId,
            &methodOffset, &methodLen,
            &payloadOffset, &payloadLen,
            &replyHostOffset, &replyHostLen,
            &result_.replyPort);

        if (status_ == Rpc_StatusCode::OK) {
            result_.method = std::string(
                reinterpret_cast<char*>(methodOffset), methodLen);
            result_.payload = std::string(
                reinterpret_cast<char*>(payloadOffset), payloadLen);
            result_.replyHost = std::string(
                reinterpret_cast<char*>(replyHostOffset), replyHostLen);
        }
        return false;
    }

    std::optional<IncomingRequest> await_resume() noexcept {
        if (status_ != Rpc_StatusCode::OK) {
            return std::nullopt;
        }
        return std::move(result_);
    }

  private:
    IncomingRequest result_;
    int32_t status_ = Rpc_StatusCode::OK;
};

} // namespace faabric::rpc