#pragma once

#include <faasrpc/Status.h>
#include <faasrpc/Task.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace faabric::rpc {

class ServerContext
{
  public:
    ServerContext() = default;
};

class Service
{
  public:
    virtual ~Service() = default;

    virtual faabric::rpc::Task<faabric::rpc::Status> HandleCall(
        const std::string& method,
        const uint8_t* reqData,
        size_t reqLen,
        std::vector<uint8_t>& respData) = 0;

    const std::vector<std::string>& Methods() const
    {
        return methods_;
    }

  protected:
    void AddMethod(const std::string& name)
    {
        methods_.push_back(name);
    }

  private:
    std::vector<std::string> methods_;
};

} // namespace faabric::rpc