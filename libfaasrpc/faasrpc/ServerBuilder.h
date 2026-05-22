#pragma once

#include <faasrpc/Server.h>
#include <faasrpc/Service.h>

#include <memory>
#include <vector>

namespace faabric::rpc {

class ServerBuilder {
public:
    ServerBuilder() = default;

    ServerBuilder& registerService(std::unique_ptr<Service> service);

    std::unique_ptr<Server> build();

private:
    std::vector<std::unique_ptr<Service>> services_;
};

} // namespace faabric::rpc