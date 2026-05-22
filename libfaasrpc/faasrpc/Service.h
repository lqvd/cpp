#pragma once

#include <faasrpc/Task.h>
#include <cstdint>
#include <string>
#include <vector>

namespace faabric::rpc {

struct Rpc_Status {
    int code;
    std::string message;
    bool ok() const { return code == 0; }
};

class ServerContext {};

class Service {
public:
    virtual ~Service() = default;

    virtual faabric::rpc::Task<Rpc_Status> HandleCall(
        const std::string& method,
        const uint8_t* reqData,
        size_t reqLen,
        std::vector<uint8_t>& respData) = 0;

    const std::vector<std::string>& Methods() const { return methods_; }

protected:
    void AddMethod(const std::string& name) { methods_.push_back(name); }

private:
    std::vector<std::string> methods_;
};

} // namespace faabric::rpc