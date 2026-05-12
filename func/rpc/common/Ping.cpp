#include "Ping.h"

#include <faasrpc/RpcCall.h>
#include <rpc.h>

bool PingResponse::ParseFromArray(const void* data, int len)
{
    if (data == nullptr || len <= 0) {
        return false;
    }
    int copyLen = len < 63 ? len : 63;
    memcpy(message, data, copyLen);
    message[copyLen] = '\0';
    status = 0;
    return true;
}

faabric::rpc::RpcCall<PingResponse> PingSvcStub::Ping(const char* payload)
{
    int32_t requestId = 0;
    int32_t status = __faasm_rpc_unary_start(
        channelId,
        "ping",
        reinterpret_cast<const uint8_t*>(payload),
        static_cast<int32_t>(strlen(payload)),
        &requestId,
        -1);

    if (status != Rpc_StatusCode::OK) {
        printf("[WASM] unary_start failed: %d\n", status);
    }

    printf("[WASM] Dispatched ping request=%d payload='%s'\n",
            requestId, payload);

    return faabric::rpc::RpcCall<PingResponse>{ requestId };
}

faabric::rpc::RpcCall<PingResponse> PingSvcStub::PingSlow(const char* payload)
{
    int32_t requestId = 0;
    int32_t status = __faasm_rpc_unary_start(
        channelId,
        "ping_slow",
        reinterpret_cast<const uint8_t*>(payload),
        static_cast<int32_t>(strlen(payload)),
        &requestId,
        -1);

    if (status != Rpc_StatusCode::OK) {
        printf("[WASM] unary_start failed (ping_slow): %d\n", status);
    }

    return faabric::rpc::RpcCall<PingResponse>{ requestId };
}