#include <faasrpc/Server.h>

#include <faasrpc/RpcReceive.h>
#include <faasrpc/Task.h>
#include <rpc.h>

#include <coroutine>
#include <cstring>
#include <string>
#include <vector>

namespace faabric::rpc {

Server::Server(std::unique_ptr<Service> service)
    : svc(std::move(service))
{}


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
            req.replyHost.c_str(), req.replyPort,
            status.code,
            respData.data(), respData.size(),
            status.message.c_str(), status.message.size());
    }
}

faabric::rpc::Task<Rpc_Status> Server::dispatch(
  const std::string& method,
  const uint8_t* payload,
  size_t payloadLen,
  std::vector<uint8_t>& respData)
{
    for (const auto& m : svc->Methods()) {
        if (m == method) {
            co_return co_await svc->HandleCall(method, payload,
                                                payloadLen, respData);
        }
    }
    co_return Rpc_Status{UNIMPLEMENTED, "No service for method: " + method};
}

} // namespace faabric::rpc