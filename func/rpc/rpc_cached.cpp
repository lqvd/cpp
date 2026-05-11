#include <faasrpc/Task.h>
#include <faasrpc/RpcCall.h>
#include <rpc.h>

#include "Ping.h"

#include <coroutine>
#include <cstdio>
#include <cstring>

faabric::rpc::Task<void> runCached(int32_t channelId)
{
    PingSvcStub stub(channelId);

    // Fast response should be ready by the time migration happens.
    auto fast = stub.Ping("cached fast");

    // Slow response creates the migration point.
    auto slow = stub.PingSlow("cached slow");

    // Await slow... migration happens here.
    PingResponse resp_slow = co_await slow;
    printf("[WASM] Slow complete. Response: '%s'\n", resp_slow.message);

    // Fast should be cached across migration.
    PingResponse resp_fast = co_await fast;
    printf("[WASM] Fast (cached) complete. Response: '%s'\n", resp_fast.message);

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

    auto task = new faabric::rpc::Task<void>(runCached(channelId));
    task->resume();

    Rpc_ChannelClose(channelId);
    task->destroy();
    return 0;
}