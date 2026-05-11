#include <faasrpc/Task.h>
#include <faasrpc/RpcCall.h>
#include <rpc.h>

#include "Ping.h"

#include <coroutine>
#include <cstdio>
#include <cstring>

faabric::rpc::Task<void> runPending(int32_t channelId)
{
    PingSvcStub stub(channelId);

    auto a = stub.PingSlow("pending A");
    auto b = stub.PingSlow("pending B");

    PingResponse resp_a = co_await a;
    printf("[WASM] Pending A complete. Response: '%s'\n", resp_a.message);

    PingResponse resp_b = co_await b;
    printf("[WASM] Pending B complete. Response: '%s'\n", resp_b.message);

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

    auto task = new faabric::rpc::Task<void>(runPending(channelId));
    task->resume();

    Rpc_ChannelClose(channelId);
    task->destroy();
    return 0;
}