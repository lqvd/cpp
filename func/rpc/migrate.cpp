#include <faasm/faasm.h>
#include <faasrpc/Task.h>
#include <faasrpc/RpcCall.h>
#include <faasrpc/coro_trampoline.h>
#include <rpc.h>

#include "Ping.pb.h"
#include "Ping.faabric.h"

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

faabric::rpc::Task<void> runMigrationBenchmark(int32_t channelId,
                                               std::string& output)
{
    rpc::PingSvc::Stub stub(channelId);

    printf("[WASM] --- Dispatching RPC A sync ---\n");
    rpc::PingResponse resp_a = co_await stub.Ping("Hello from host A");
    output += resp_a.message() + "\n";

    printf("[WASM] RPC A complete. Response: '%s'\n",
           resp_a.message().c_str());

    printf("[WASM] --- Dispatching RPC B async ---\n");
    auto ping_b = stub.PingAsync("Hello from wherever we are now");

    printf("[WASM] Waiting for RPC B (migration point 2)...\n");
    rpc::PingResponse resp_b = co_await ping_b;
    output += resp_b.message() + "\n";

    printf("[WASM] RPC B complete. Response: '%s'\n",
           resp_b.message().c_str());

    printf("[WASM] --- Fan-out: dispatching C and D simultaneously ---\n");
    auto ping_c = stub.PingAsync("Fan-out C");
    auto ping_d = stub.PingAsync("Fan-out D");

    printf("[WASM] Awaiting C (both C and D are in flight)...\n");
    rpc::PingResponse resp_c = co_await ping_c;
    output += resp_c.message() + "\n";

    printf("[WASM] Awaiting D...\n");
    rpc::PingResponse resp_d = co_await ping_d;
    output += resp_d.message() + "\n";

    printf("[WASM] Fan-out complete. C='%s' D='%s'\n",
           resp_c.message().c_str(),
           resp_d.message().c_str());

    printf("[WASM] All RPC calls complete.\n");
    co_return;
}

int main(int argc, char* argv[])
{
    printf("Starting coroutine RPC migration benchmark\n");

    int32_t channelId = 0;
    int32_t createStatus =
        Rpc_ChannelCreate("faabric://rpc/PingSvc", &channelId);

    if (createStatus != Rpc_StatusCode::OK) {
        printf("[WASM] Rpc_ChannelCreate failed: %d\n", createStatus);
        return 1;
    }

    printf("[WASM] Channel created: %d\n", channelId);

    std::string output;

    auto* task =
        new faabric::rpc::Task<void>(runMigrationBenchmark(channelId, output));

    task->resume();

    Rpc_ChannelClose(channelId);
    task->destroy();

    faasmSetOutput(output.c_str(), static_cast<long>(output.size()));

    printf("Benchmark finished successfully\n");
    return 0;
}