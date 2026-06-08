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
#include <string>
#include <vector>

static int parseFanOut(int argc, char* argv[])
{
    if (argc < 2) {
        return 4;
    }
    int n = std::atoi(argv[1]);
    return (n > 0) ? n : 1;
}

// Output format (CSV): received,total,resp_seq,resp_0,...,resp_{N-1}
// Early failure:       received,total,<error>
//
// Structure: 1 sequential ping (migration point 1) followed by fanOut
// async pings all dispatched before any are awaited, then awaited in
// index order (each co_await is a potential migration point).
faabric::rpc::Task<void> runMigrationBenchmark(
    std::shared_ptr<faabric::rpc::Channel> channel,
    int fanOut,
    std::string& output)
{
    rpc::PingSvc::Stub stub(channel);
    int received = 0;
    const int total = 1 + fanOut;
    std::vector<std::string> responses;
    responses.reserve(total);

    // --- Sequential ping (migration point 1) ---
    printf("[WASM] Dispatching sequential ping\n");

    faabric::rpc::ClientContext ctxSeq;
    rpc::PingRequest reqSeq;
    reqSeq.set_message("seq");

    auto resultSeq = co_await stub.Ping(&ctxSeq, reqSeq);
    if (!resultSeq.ok()) {
        output = std::to_string(received) + "," + std::to_string(total) +
         ",seq failed: " + std::string(resultSeq.status().message());
        co_return;
    }
    received++;
    responses.push_back(resultSeq.value().message());
    printf("[WASM] Sequential ping complete\n");

    // --- Fan-out: dispatch all N before awaiting any ---
    // Contexts and requests must outlive all subsequent co_awaits.
    printf("[WASM] Dispatching %d fan-out pings\n", fanOut);

    std::vector<faabric::rpc::ClientContext> ctxs(fanOut);
    std::vector<rpc::PingRequest> reqs(fanOut);

    using PingHandle = decltype(stub.AsyncPing(&ctxs[0], reqs[0]));
    std::vector<PingHandle> calls;
    calls.reserve(fanOut);

    for (int i = 0; i < fanOut; i++) {
        reqs[i].set_message("fan-out-" + std::to_string(i));
        calls.push_back(stub.AsyncPing(&ctxs[i], reqs[i]));
    }

    // Await in index order; migration may fire at any of these points.
    for (int i = 0; i < fanOut; i++) {
        printf("[WASM] Awaiting fan-out ping %d / %d\n", i, fanOut - 1);
        auto result = co_await calls[i];
        if (!result.ok()) {
            output = std::to_string(received) + "," + std::to_string(total) +
              ",fan-out-" + std::to_string(i) +
              " failed: " + std::string(result.status().message());
            co_return;
        }
        received++;
        responses.push_back(result.value().message());
    }

    output = std::to_string(received) + "," + std::to_string(total);
    for (const auto& r : responses) {
        output += "," + r;
    }
    co_return;
}

int main(int argc, char* argv[])
{
    int fanOut = parseFanOut(argc, argv);
    printf("Starting migration benchmark: 1 sequential + %d fan-out\n", fanOut);

    std::shared_ptr<faabric::rpc::Channel> channel;
    faabric::rpc::Status status =
      faabric::rpc::CreateChannel(rpc::PingSvc::ServiceUri, &channel);

    if (!status.ok()) {
        std::string out = "0,0,channel failed: ";
        out += std::string(status.message());
        faasmSetOutput(out.c_str(), static_cast<long>(out.size()));
        return 1;
    }

    std::string output;
    auto* task = new faabric::rpc::Task<void>(
      runMigrationBenchmark(channel, fanOut, output));

    task->resume();
    task->destroy();

    faasmSetOutput(output.c_str(), static_cast<long>(output.size()));
    printf("Benchmark finished. output=%s\n", output.c_str());
    return 0;
}