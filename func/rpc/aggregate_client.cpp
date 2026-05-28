#include <faasm/faasm.h>
#include <faasrpc/Task.h>
#include <rpc.h>

#include "Aggregator.faabric.h"
#include "Aggregator.pb.h"

#include <cstdio>
#include <string>

faabric::rpc::Task<void> runAggregatorClient(std::string& output)
{
    int32_t channelId = 0;
    int32_t createStatus =
      Rpc_ChannelCreate("faabric://rpc/AggregatorSvc", &channelId);

    if (createStatus != Rpc_StatusCode::OK) {
        printf("[AggregatorClient] Rpc_ChannelCreate failed: %d\n",
               createStatus);
        output = "AggregatorClient failed to create channel\n";
        co_return;
    }

    rpc::AggregatorSvc::Stub stub(channelId);

    printf("[AggregatorClient] Calling AggregatorSvc.Aggregate\n");

    rpc::AggregateResponse resp =
      co_await stub.Aggregate("hello-from-client");

    output += resp.message();
    output += "\n";

    printf("[AggregatorClient] Response: '%s'\n", resp.message().c_str());

    Rpc_ChannelClose(channelId);

    co_return;
}

int main()
{
    printf("[AggregatorClient] Starting\n");

    std::string output;

    auto task = runAggregatorClient(output);
    bool stillRunning = task.resume();

    if (stillRunning) {
        printf("[AggregatorClient] ERROR: coroutine unexpectedly suspended\n");
        return 1;
    }

    faasmSetOutput(output.c_str(), static_cast<long>(output.size()));

    printf("[AggregatorClient] Finished\n");
    return 0;
}