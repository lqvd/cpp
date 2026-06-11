#include <faasrpc/ServerBuilder.h>
#include <faasrpc/Driver.h>

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
    faabric::rpc::setServerRoot(task.get_handle());
    faabric::rpc::runServerDriver();
}

} // namespace faabric::rpc