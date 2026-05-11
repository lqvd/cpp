#include <faasrpc/Task.h>
#include <faasrpc/RpcCall.h>
#include <rpc.h>

#include "Ping.h"

#include <coroutine>
#include <cstdio>
#include <cstring>

faabric::rpc::Task<void> runPing(int32_t channelId)
{
    PingSvcStub stub(channelId);

    auto ping = stub.Ping("Hello from rpc_ping");
    PingResponse resp = co_await ping;
    printf("[WASM] Ping complete. Response: '%s'\n", resp.message);

    co_return;
}

int main(int argc, char* argv[])
{
    int32_t channelId = 0;
    int32_t status = Rpc_ChannelCreate("faabric://127.0.0.1", &channelId);
    if (status != Rpc_StatusCode::OK) {
        printf("[WASM] Rpc_ChannelCreate failed: %d\n", status);
        return 1;
    }

    auto task = new faabric::rpc::Task<void>(runPing(channelId));
    task->resume();

    Rpc_ChannelClose(channelId);
    task->destroy();
    return 0;
}