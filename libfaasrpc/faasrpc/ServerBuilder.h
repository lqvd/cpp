#pragma once

#include <faasrpc/Server.h>
#include <faasrpc/Service.h>

#include <memory>

namespace faabric::rpc {

class ServerBuilder
{
  public:
    ServerBuilder() = default;

    ServerBuilder& registerService(std::unique_ptr<Service> service);

    std::unique_ptr<Server> build();

    void buildAndStart();

  private:
    std::unique_ptr<Service> service_;
};

} // namespace faabric::rpc