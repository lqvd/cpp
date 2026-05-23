#include <faasrpc/Server.h>


#include <faabric/rpc/rpc.h>
#include <faasrpc/RpcReceive.h>
#include <faasrpc/Task.h>

#include <coroutine>
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
        IncomingRequest req = co_await RpcReceive{};

        std::vector<uint8_t> respData;
        Rpc_Status status = co_await dispatch(
          req.method,
          reinterpret_cast<const uint8_t*>(req.payload.data()),
          req.payload.size(),
          respData);

        __faasm_rpc_send_response(
          req.requestId,
          req.replyHost.c_str(),
          req.replyPort,
          status.code,
          respData.data(),
          static_cast<int32_t>(respData.size()),
          status.message.c_str(),
          static_cast<int32_t>(status.message.size()));
    }
}

faabric::rpc::Task<Rpc_Status> Server::dispatch(
  const std::string& method,
  const uint8_t* payload,
  size_t payloadLen,
  std::vector<uint8_t>& respData)
{
    for (const auto& m : service_->Methods()) {
        if (m == method) {
            co_return co_await service_->HandleCall(
              method, payload, payloadLen, respData);
        }
    }

    co_return Rpc_Status{
        Rpc_StatusCode::UNIMPLEMENTED,
        "No service for method: " + method
    };
}

} // namespace faabric::rpc