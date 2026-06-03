#include <faasm/faasm.h>
#include <faasrpc/ClientContext.h>
#include <faasrpc/ServerBuilder.h>
#include <faasrpc/Channel.h>
#include <faasm/host_interface.h>

#include "composePost/proto/compose_post.faabric.h"
#include "composePost/proto/compose_post.pb.h"
#include "postStorage/proto/post_storage.faabric.h"
#include "postStorage/proto/post_storage.pb.h"
#include "benchmark.pb.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <numeric>
#include <vector>

namespace snb {

static constexpr int32_t kNumUsers     = 10;

static int64_t percentile(std::vector<int64_t>& sorted, double p)
{
    if (sorted.empty()) return 0;
    size_t idx = static_cast<size_t>(p * sorted.size());
    idx = std::min(idx, sorted.size() - 1);
    return sorted[idx];
}

faabric::rpc::Task<void> runBenchmark(int32_t numRequests)
{
    // --- resolve channels once ---
    std::shared_ptr<faabric::rpc::Channel> composeChannel;
    std::shared_ptr<faabric::rpc::Channel> storeChannel;

    if (auto s = faabric::rpc::CreateChannel(
            snb::ComposePostService::ServiceUri,
            &composeChannel); !s.ok()) {
        fprintf(stderr, "[Client] Failed to connect to ComposePost: %s\n",
                s.message().data());
        co_return;
    }

    if (auto s = faabric::rpc::CreateChannel(
            snb::PostStorageService::ServiceUri,
            &storeChannel); !s.ok()) {
        fprintf(stderr, "[Client] Failed to connect to PostStorage: %s\n",
                s.message().data());
        co_return;
    }

    auto composeStub = snb::ComposePostService::NewStub(composeChannel);
    auto storeStub   = snb::PostStorageService::NewStub(storeChannel);

    // --- run requests ---
    std::vector<int64_t> latencies;
    latencies.reserve(numRequests);

    int32_t successes = 0;
    int32_t failures  = 0;

    auto benchStart = std::chrono::steady_clock::now();

    for (int32_t i = 0; i < numRequests; ++i) {
        snb::ComposePostRequest req;
        req.set_req_id(i);
        req.set_user_id((i % kNumUsers) + 1);
        req.set_username("user_" + std::to_string((i % kNumUsers) + 1));
        req.set_text("Benchmark post number " + std::to_string(i));
        req.set_post_type(snb::PostType::POST);

        faabric::rpc::ClientContext ctx;
        auto t0   = std::chrono::steady_clock::now();
        auto call = composeStub->AsyncComposePost(&ctx, req);
        auto res  = co_await call;
        auto t1   = std::chrono::steady_clock::now();

        int64_t latNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            t1 - t0).count();
        latencies.push_back(latNs);

        if (res.ok()) {
            ++successes;
        } else {
            ++failures;
            fprintf(stderr, "[Client] Request %d failed: %s\n",
                    i, res.status().message().data());
        }
    }

    auto benchEnd = std::chrono::steady_clock::now();
    int64_t totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        benchEnd - benchStart).count();

    // --- validate: check stored post count ---
    snb::Empty empty;
    faabric::rpc::ClientContext countCtx;
    auto countCall = storeStub->AsyncGetStoredPostCount(&countCtx, empty);
    auto countResult = co_await countCall;

    int64_t storedCount = 0;
    if (countResult.ok()) {
        storedCount = countResult.value().count();
    } else {
        fprintf(stderr, "[Client] GetStoredPostCount failed: %s\n",
                countResult.status().message().data());
    }

    // --- compute stats ---
    std::sort(latencies.begin(), latencies.end());

    int64_t sum  = std::accumulate(latencies.begin(), latencies.end(), 0LL);
    int64_t mean = latencies.empty() ? 0 : sum / (int64_t)latencies.size();

    snb::BenchmarkClientResult result;
    result.set_total_requests(numRequests);
    result.set_successful_requests(successes);
    result.set_failed_requests(failures);
    result.set_total_duration_ns(totalNs);
    result.set_stored_post_count(storedCount);

    int64_t totalFromLatencies = std::accumulate(
    latencies.begin(), latencies.end(), 0LL);

    if (totalFromLatencies > 0) {
        result.set_throughput_rps(
            (double)successes / ((double)totalFromLatencies / 1e9));
    } 

    auto* stats = result.mutable_latency();
    stats->set_p50_ns (percentile(latencies, 0.50));
    stats->set_p99_ns (percentile(latencies, 0.99));
    stats->set_p999_ns(percentile(latencies, 0.999));
    stats->set_min_ns (latencies.empty() ? 0 : latencies.front());
    stats->set_max_ns (latencies.empty() ? 0 : latencies.back());
    stats->set_mean_ns(mean);

    for (int64_t l : latencies) {
        result.add_raw_latencies_ns(l);
    }

    // --- serialize to Faasm output ---
    char out[512];
    int len = snprintf(out, sizeof(out),
        "{\"total\":%d,\"ok\":%d,\"fail\":%d,"
        "\"p50_ns\":%lld,\"p99_ns\":%lld,\"p999_ns\":%lld,"
        "\"throughput\":%.2f,\"stored\":%lld}",
        result.total_requests(),
        result.successful_requests(),
        result.failed_requests(),
        (long long)result.latency().p50_ns(),
        (long long)result.latency().p99_ns(),
        (long long)result.latency().p999_ns(),
        result.throughput_rps(),
        (long long)result.stored_post_count());
    faasmSetOutput(out, len);

    fprintf(stderr,
            "[Client] Done: %d/%d ok, p50=%.2fms p99=%.2fms p999=%.2fms "
            "throughput=%.1f rps stored=%lld\n",
            successes, numRequests,
            stats->p50_ns()  / 1e6,
            stats->p99_ns()  / 1e6,
            stats->p999_ns() / 1e6,
            result.throughput_rps(),
            (long long)storedCount);
}

} // namespace snb

int main(int argc, char* argv[])
{
    int32_t numRequests = 10;  // default
    if (argc > 1) {
        numRequests = std::atoi(argv[1]);
    }

    auto task = snb::runBenchmark(numRequests);
    while (task.resume()) {}
    task.promise().result();
    return 0;
}