#include <faasrpc/ServerBuilder.h>

namespace faabric::rpc {

ServerBuilder& ServerBuilder::registerService(std::unique_ptr<Service> service)
{
    services_.push_back(std::move(service));
    return *this;
}

std::unique_ptr<Server> ServerBuilder::build()
{
    return std::make_unique<Server>(std::move(services_));
}

} // namespace faabric::rpc