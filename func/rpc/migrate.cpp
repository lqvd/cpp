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

faabric::rpc::Task<void> runMigrationBenchmark(
    std::shared_ptr<faabric::rpc::Channel> channel,
    std::string& output)
{
    rpc::PingSvc::Stub stub(channel);

    faabric::rpc::ClientContext ctxA;

    rpc::PingRequest reqA;
    reqA.set_message("Hello from host A");

    printf("[WASM] --- Dispatching RPC A sync ---\n");

    auto resultA = co_await stub.Ping(&ctxA, reqA);
    if (!resultA.ok()) {
        output += "RPC A failed: ";
        output += resultA.status().message();
        output += "\n";
        co_return;
    }

    const rpc::PingResponse& respA = resultA.value();
    output += respA.message() + "\n";

    printf("[WASM] RPC A complete. Response: '%s'\n",
           respA.message().c_str());

    printf("[WASM] --- Dispatching RPC B async ---\n");

    faabric::rpc::ClientContext ctxB;

    rpc::PingRequest reqB;
    reqB.set_message("Hello from wherever we are now");

    auto pingB = stub.PingAsync(&ctxB, reqB);

    printf("[WASM] Waiting for RPC B (migration point 2)...\n");

    auto resultB = co_await pingB;
    if (!resultB.ok()) {
        output += "RPC B failed: ";
        output += resultB.status().message();
        output += "\n";
        co_return;
    }

    const rpc::PingResponse& respB = resultB.value();
    output += respB.message() + "\n";

    printf("[WASM] RPC B complete. Response: '%s'\n",
           respB.message().c_str());

    printf("[WASM] --- Fan-out: dispatching C and D simultaneously ---\n");

    faabric::rpc::ClientContext ctxC;
    faabric::rpc::ClientContext ctxD;

    rpc::PingRequest reqC;
    reqC.set_message("Fan-out C");

    rpc::PingRequest reqD;
    reqD.set_message("Fan-out D");

    auto pingC = stub.PingAsync(&ctxC, reqC);
    auto pingD = stub.PingAsync(&ctxD, reqD);

    printf("[WASM] Awaiting C (both C and D are in flight)...\n");

    auto resultC = co_await pingC;
    if (!resultC.ok()) {
        output += "RPC C failed: ";
        output += resultC.status().message();
        output += "\n";
        co_return;
    }

    const rpc::PingResponse& respC = resultC.value();
    output += respC.message() + "\n";

    printf("[WASM] Awaiting D...\n");

    auto resultD = co_await pingD;
    if (!resultD.ok()) {
        output += "RPC D failed: ";
        output += resultD.status().message();
        output += "\n";
        co_return;
    }

    const rpc::PingResponse& respD = resultD.value();
    output += respD.message() + "\n";

    printf("[WASM] Fan-out complete. C='%s' D='%s'\n",
           respC.message().c_str(),
           respD.message().c_str());

    printf("[WASM] All RPC calls complete.\n");
    co_return;
}

int main(int argc, char* argv[])
{
    printf("Starting coroutine RPC migration benchmark\n");

    std::shared_ptr<faabric::rpc::Channel> channel;
    faabric::rpc::Status status =
      faabric::rpc::CreateChannel(rpc::PingSvc::ServiceUri, &channel);

    if (!status.ok()) {
        std::string output = "Failed to create RPC channel: ";
        output += status.message();
        output += "\n";

        faasmSetOutput(output.c_str(), static_cast<long>(output.size()));
        return 1;
    }

    std::string output;

    auto* task =
      new faabric::rpc::Task<void>(runMigrationBenchmark(channel, output));

    task->resume();
    task->destroy();

    faasmSetOutput(output.c_str(), static_cast<long>(output.size()));

    printf("Benchmark finished successfully\n");
    return 0;
}