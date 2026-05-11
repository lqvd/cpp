#include <faasrpc/Task.h>
#include <faasrpc/RpcCall.h>
#include <faasrpc/coro_trampoline.h>
#include <rpc.h>

#include "Ping.h"

#include <coroutine>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Coroutine entry point

// If migration happens at co_await ping_a:
//   - Frame is snapshotted (contains channelId, ping_b RpcCall etc.)
//   - __faasm_rpc_coro_trampoline is registered as the re-entry point
//   - Host B restores linear memory, calls trampoline(frameOffset)
//   - Coroutine resumes from after co_await ping_a
//   - Response for ping_a is proxied from host A via forwarding table
//   - ping_b is dispatched on host B
// Returns Task<void> — the executor calls task.resume() once to start it.

faabric::rpc::Task<void> runMigrationBenchmark(int32_t channelId)
{
    PingSvcStub stub(channelId);

    printf("[WASM] --- Dispatching RPC A ---\n");

    auto ping_a = stub.Ping("Hello from host A");

    printf("[WASM] Waiting for RPC A (migration point 1)...\n");
    PingResponse resp_a = co_await ping_a;

    printf("[WASM] RPC A complete. Response: '%s'\n", resp_a.message);

    printf("[WASM] --- Dispatching RPC B ---\n");
    auto ping_b = stub.Ping("Hello from wherever we are now");

    printf("[WASM] Waiting for RPC B (migration point 2)...\n");
    PingResponse resp_b = co_await ping_b;

    printf("[WASM] RPC B complete. Response: '%s'\n", resp_b.message);

    printf("[WASM] --- Fan-out: dispatching C and D simultaneously ---\n");
    auto ping_c = stub.Ping("Fan-out C");
    auto ping_d = stub.Ping("Fan-out D");

    printf("[WASM] Awaiting C (both C and D are in flight)...\n");
    PingResponse resp_c = co_await ping_c;

    printf("[WASM] Awaiting D...\n");
    PingResponse resp_d = co_await ping_d;

    printf("[WASM] Fan-out complete. C='%s' D='%s'\n",
           resp_c.message, resp_d.message);

    printf("[WASM] All RPC calls complete.\n");
    co_return;
}

int main(int argc, char* argv[])
{
    printf("Starting coroutine RPC migration benchmark\n");

    // Create channel
    int32_t channelId = 0;
    int32_t createStatus =
        Rpc_ChannelCreate("faabric://127.0.0.1", &channelId);

    if (createStatus != Rpc_StatusCode::OK) {
        printf("[WASM] Rpc_ChannelCreate failed: %d\n", createStatus);
        return 1;
    }

    printf("[WASM] Channel created: %d\n", channelId);

    auto* task = new faabric::rpc::Task<void>(runMigrationBenchmark(channelId));
    task->resume();

    Rpc_ChannelClose(channelId);
    task->destroy();

    printf("Benchmark finished successfully\n");
    return 0;
}
