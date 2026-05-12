#pragma once

#include <faasrpc/Task.h>
#include <faasrpc/RpcCall.h>
#include <rpc.h>

#include <cstdint>

struct PingResponse {
    int32_t status = 0;
    char message[64] = {};

    bool ParseFromArray(const void* data, int len);
};

class PingSvcStub {
  public:
    explicit PingSvcStub(int32_t channelId)
      : channelId(channelId) {}

    faabric::rpc::RpcCall<PingResponse> Ping(const char* payload);
    
    faabric::rpc::RpcCall<PingResponse> PingSlow(const char* payload); 

  private:
    int32_t channelId;
};