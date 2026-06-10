#include <faasrpc/Server.h>

#include <faabric/rpc/rpc.h>
#include <faasrpc/RpcReceive.h>
#include <faasrpc/Task.h>

#include <coroutine>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

namespace faabric::rpc {

Server::Server(std::unique_ptr<Service> service)
  : service_(std::move(service))
{
    if (!service_) {
        throw std::runtime_error("Cannot construct Server with null service");
    }
}

faabric::rpc::Task<void> Server::serveForever()
{
    while (true) {
        auto maybeReq = co_await RpcReceive{};
        if (!maybeReq) {
            break;
        }

        IncomingRequest& req = *maybeReq;

        std::vector<uint8_t> respData;

        auto dispatchTask = 
          dispatch(
            req.method,
            reinterpret_cast<const uint8_t*>(req.payload.data()),
            req.payload.size(),
            respData);
        faabric::rpc::Status status = co_await dispatchTask;

        const std::string statusMessage(status.message());

        int32_t sendStatus = __faasm_rpc_send_response(
          req.requestId,
          req.replyHost.c_str(),
          req.replyPort,
          status.code(),
          respData.empty() ? nullptr : respData.data(),
          static_cast<int32_t>(respData.size()),
          statusMessage.empty() ? nullptr : statusMessage.c_str(),
          static_cast<int32_t>(statusMessage.size()));

        if (sendStatus != Rpc_StatusCode::OK) {
            printf("[RPC Server] Failed to send response for request %u: %d\n",
                   req.requestId,
                   sendStatus);
        }
    }

    co_return;
}

faabric::rpc::Task<faabric::rpc::Status> Server::dispatch(
  const std::string& method,
  const uint8_t* payload,
  size_t payloadLen,
  std::vector<uint8_t>& respData)
{
    for (const auto& m : service_->Methods()) {
        if (m == method) {
            auto callTask =
              service_->HandleCall(method, payload, payloadLen, respData);

            faabric::rpc::Status status = co_await callTask;
            co_return status;
        }
    }

    co_return faabric::rpc::Status{
        Rpc_StatusCode::UNIMPLEMENTED,
        "No service for method: " + method
    };
}

} // namespace faabric::rpc