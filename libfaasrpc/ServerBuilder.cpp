#include <faasrpc/ServerBuilder.h>

#include <stdexcept>
#include <utility>

namespace faabric::rpc {

ServerBuilder& ServerBuilder::registerService(std::unique_ptr<Service> service)
{
    if (!service) {
        throw std::runtime_error("Cannot register null RPC service");
    }

    if (service_) {
        throw std::runtime_error("ServerBuilder only supports one service");
    }

    service_ = std::move(service);
    return *this;
}

std::unique_ptr<Server> ServerBuilder::build()
{
    if (!service_) {
        throw std::runtime_error("Cannot build RPC server with no service");
    }

    return std::make_unique<Server>(std::move(service_));
}

void ServerBuilder::buildAndStart()
{
    auto server = build();

    auto task = server->serveForever();
    task.resume();
}

} // namespace faabric::rpc