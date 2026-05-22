#pragma once

#include <faasrpc/Service.h>
#include <faasrpc/Task.h>

#include <memory>
#include <vector>

namespace faabric::rpc {

class Server {
public:
    explicit Server(std::unique_ptr<Service> service);
    faabric::rpc::Task<void> serveForever();

private:
    std::unique_ptr<Service> svc;

    faabric::rpc::Task<Rpc_Status> dispatch(
        const std::string& method,
        const uint8_t* payload,
        size_t payloadLen,
        std::vector<uint8_t>& respData);
};

} // namespace faabric::rpc