#include <faasm/faasm.h>

#include <faasrpc/Channel.h>
#include <faasrpc/ClientContext.h>

#include "benchmark.pb.h"
#include "composePost/proto/compose_post.faabric.h"
#include "composePost/proto/compose_post.pb.h"
#include "postStorage/proto/post_storage.faabric.h"
#include "postStorage/proto/post_storage.pb.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

namespace snb {

static constexpr int32_t kNumUsers = 10;

static int64_t durationNs(std::chrono::steady_clock::duration d)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
}

static int64_t percentile(const std::vector<int64_t>& sorted, double p)
{
    if (sorted.empty()) {
        return 0;
    }

    size_t idx = static_cast<size_t>(std::ceil(p * sorted.size()));

    if (idx == 0) {
        idx = 1;
    }

    idx -= 1;
    idx = std::min(idx, sorted.size() - 1);

    return sorted[idx];
}

faabric::rpc::Task<void> runBenchmark(int32_t numRequests)
{
    // ---------------------------------------------------------------------
    // Resolve channels once, outside the measured loop.
    // ---------------------------------------------------------------------

    std::shared_ptr<faabric::rpc::Channel> composeChannel;
    std::shared_ptr<faabric::rpc::Channel> storeChannel;

    if (auto s = faabric::rpc::CreateChannel(
            snb::ComposePostService::ServiceUri,
            &composeChannel);
        !s.ok()) {
        fprintf(stderr,
                "[Client] Failed to connect to ComposePost: %s\n",
                std::string(s.message()).c_str());
        co_return;
    }

    if (auto s = faabric::rpc::CreateChannel(
            snb::PostStorageService::ServiceUri,
            &storeChannel);
        !s.ok()) {
        fprintf(stderr,
                "[Client] Failed to connect to PostStorage: %s\n",
                std::string(s.message()).c_str());
        co_return;
    }

    auto composeStub = snb::ComposePostService::NewStub(composeChannel);
    auto storeStub = snb::PostStorageService::NewStub(storeChannel);

    // ---------------------------------------------------------------------
    // Run benchmark.
    // ---------------------------------------------------------------------

    snb::BenchmarkClientResult result;

    std::vector<int64_t> latencies;
    latencies.reserve(numRequests);

    int32_t successes = 0;
    int32_t failures = 0;

    auto benchStart = std::chrono::steady_clock::now();

    for (int32_t i = 0; i < numRequests; ++i) {
        snb::ComposePostRequest req;
        req.set_req_id(i);
        req.set_user_id((i % kNumUsers) + 1);
        req.set_username("user_" + std::to_string((i % kNumUsers) + 1));
        req.set_text("Benchmark post number " + std::to_string(i));
        req.set_post_type(snb::PostType::POST);

        faabric::rpc::ClientContext ctx;

        auto t0 = std::chrono::steady_clock::now();

        auto call = composeStub->AsyncComposePost(&ctx, req);
        auto res = co_await call;

        auto t1 = std::chrono::steady_clock::now();

        int64_t startNs = durationNs(t0 - benchStart);
        int64_t endNs = durationNs(t1 - benchStart);
        int64_t latNs = endNs - startNs;

        latencies.push_back(latNs);

        if (res.ok()) {
            ++successes;
        } else {
            ++failures;
            fprintf(stderr, "[Client] Request %d failed: %s\n", i,
                    std::string(res.status().message()).c_str());
        }
    }

    auto benchEnd = std::chrono::steady_clock::now();
    int64_t totalNs = durationNs(benchEnd - benchStart);

    // ---------------------------------------------------------------------
    // Validate by checking stored post count.
    // ---------------------------------------------------------------------

    snb::Empty empty;
    faabric::rpc::ClientContext countCtx;

    auto countCall = storeStub->AsyncGetStoredPostCount(&countCtx, empty);
    auto countResult = co_await countCall;

    int64_t storedCount = 0;

    if (countResult.ok()) {
        storedCount = countResult.value().count();
    } else {
        std::string err(countResult.status().message());
        fprintf(stderr, "[Client] GetStoredPostCount failed: %s\n", err.c_str());
    }

    // ---------------------------------------------------------------------
    // Compute latency statistics.
    // ---------------------------------------------------------------------

    auto sortedLatencies = latencies;
    std::sort(sortedLatencies.begin(), sortedLatencies.end());

    int64_t sum = std::accumulate(latencies.begin(), latencies.end(), 0LL);
    int64_t mean = latencies.empty()
                       ? 0
                       : sum / static_cast<int64_t>(latencies.size());

    result.set_total_requests(numRequests);
    result.set_successful_requests(successes);
    result.set_failed_requests(failures);
    result.set_total_duration_ns(totalNs);
    result.set_stored_post_count(storedCount);

    if (totalNs > 0) {
        result.set_throughput_rps(
            static_cast<double>(successes) /
            (static_cast<double>(totalNs) / 1e9));
    }

    auto* stats = result.mutable_latency();
    stats->set_p50_ns(percentile(sortedLatencies, 0.50));
    stats->set_p95_ns(percentile(sortedLatencies, 0.95));
    stats->set_p99_ns(percentile(sortedLatencies, 0.99));
    stats->set_p999_ns(percentile(sortedLatencies, 0.999));
    stats->set_min_ns(sortedLatencies.empty() ? 0 : sortedLatencies.front());
    stats->set_max_ns(sortedLatencies.empty() ? 0 : sortedLatencies.back());
    stats->set_mean_ns(mean);

    // Keep raw latencies in original request order, not sorted order.
    for (int64_t latNs : latencies) {
        result.add_raw_latencies_ns(latNs);
    }

    // ---------------------------------------------------------------------
    // Return protobuf result as raw binary output.
    // ---------------------------------------------------------------------

    char buf[512];
    snprintf(buf, sizeof(buf),
        "%d,%d,%d,%lld,%.3f,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld",
        successes,
        failures,
        numRequests,
        (long long)totalNs,
        result.throughput_rps(),
        (long long)storedCount,
        (long long)stats->p50_ns(),
        (long long)stats->p95_ns(),
        (long long)stats->p99_ns(),
        (long long)stats->p999_ns(),
        (long long)stats->min_ns(),
        (long long)stats->max_ns(),
        (long long)stats->mean_ns());

    faasmSetOutput(buf, strlen(buf));

    co_return;
}

} // namespace snb

int main(int argc, char* argv[])
{
    int32_t numRequests = 10;

    if (argc > 1) {
        numRequests = std::atoi(argv[1]);
    }

    if (numRequests <= 0) {
        fprintf(stderr,
                "[Client] Invalid numRequests=%d, using default 10\n",
                numRequests);
        numRequests = 10;
    }

    auto task = snb::runBenchmark(numRequests);

    while (task.resume()) {}

    task.promise().result();

    return 0;
}