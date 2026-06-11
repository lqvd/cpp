// ChatGPT was used in creating this file.

#include <faasm/faasm.h>

#include <faasrpc/Channel.h>
#include <faasrpc/ClientContext.h>
#include <faasrpc/RpcCall.h>
#include <faasrpc/Status.h>
#include <faasrpc/Task.h>

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
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace snb {

struct Args
{
    int totalRequests = 1000;
    int concurrency = 1;

    int textBytes = 128;
    int mentionCount = 2;
    int urlCount = 1;

    int userCount = 100;
    int seed = 1;

    int warmupRequests = 100;
    bool verifyStorage = false;
};

static int64_t durationNs(std::chrono::steady_clock::duration d)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
}

static Args parseArgs(int argc, char* argv[])
{
    Args args;

    if (argc >= 2) {
        args.totalRequests = std::max(1, std::atoi(argv[1]));
    }

    if (argc >= 3) {
        args.concurrency = std::max(1, std::atoi(argv[2]));
    }

    if (argc >= 4) {
        args.textBytes = std::max(0, std::atoi(argv[3]));
    }

    if (argc >= 5) {
        args.mentionCount = std::max(0, std::atoi(argv[4]));
    }

    if (argc >= 6) {
        args.urlCount = std::max(0, std::atoi(argv[5]));
    }

    if (argc >= 7) {
        args.userCount = std::max(1, std::atoi(argv[6]));
    }

    if (argc >= 8) {
        args.seed = std::atoi(argv[7]);
    }

    if (argc >= 9) {
        args.warmupRequests = std::max(0, std::atoi(argv[8]));
    }

    if (argc >= 10) {
        args.verifyStorage = std::atoi(argv[9]) != 0;
    }

    args.concurrency = std::min(args.concurrency, args.totalRequests);
    return args;
}

static std::string csvSafe(std::string s)
{
    for (char& c : s) {
        if (c == ',' || c == '\n' || c == '\r') {
            c = '_';
        }
    }

    return s;
}

static void appendCsvHeader(std::ostringstream& out)
{
    out << "request_idx,batch_idx,slot_idx,concurrency,text_bytes,"
        << "mention_count,url_count,user_count,seed,"
        << "start_ns,end_ns,latency_ns,ok,status\n";
}

static void setOutput(const std::string& s)
{
    faasmSetOutput(s.c_str(), static_cast<long>(s.size()));
}

static void setErrorOutput(const std::string& msg)
{
    std::ostringstream out;
    out << "# benchmark_client_version=per_request_csv_v1\n";
    appendCsvHeader(out);
    out << "# error," << csvSafe(msg) << "\n";
    setOutput(out.str());
}

static uint64_t mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static int boundedInt(const Args& args, int requestIdx, int stream, int upper)
{
    const uint64_t x =
      mix64(static_cast<uint64_t>(requestIdx) ^
            (static_cast<uint64_t>(args.seed) << 32) ^
            static_cast<uint64_t>(stream));

    return static_cast<int>(x % static_cast<uint64_t>(upper));
}

static int64_t userIdForRequest(const Args& args, int requestIdx)
{
    return 1 + boundedInt(args, requestIdx, 0, args.userCount);
}

static std::string usernameForUser(int64_t userId)
{
    return "user" + std::to_string(userId);
}

static std::string makeText(const Args& args, int requestIdx)
{
    std::ostringstream ss;

    ss << "post-" << requestIdx << " ";

    for (int i = 0; i < args.mentionCount; i++) {
        const int64_t mentionedUser =
          1 + boundedInt(args, requestIdx, 100 + i, args.userCount);

        ss << "@" << usernameForUser(mentionedUser) << " ";
    }

    for (int i = 0; i < args.urlCount; i++) {
        ss << "http://example.com/post/" << requestIdx << "/" << i << " ";
    }

    std::string text = ss.str();

    if (static_cast<int>(text.size()) > args.textBytes) {
        text.resize(static_cast<size_t>(args.textBytes));
        return text;
    }

    while (static_cast<int>(text.size()) < args.textBytes) {
        text += "x";
    }

    return text;
}

static void fillComposeRequest(snb::ComposePostRequest& req,
                               const Args& args,
                               int requestIdx,
                               int64_t reqId)
{
    const int64_t userId = userIdForRequest(args, requestIdx);

    req.set_req_id(reqId);
    req.set_user_id(userId);
    req.set_username(usernameForUser(userId));
    req.set_text(makeText(args, requestIdx));
    req.set_post_type(snb::PostType::POST);
}

static void appendCsvRow(std::ostringstream& out,
                         int requestIdx,
                         int batchIdx,
                         int slotIdx,
                         const Args& args,
                         int64_t startNs,
                         int64_t endNs,
                         bool ok,
                         const std::string& status)
{
    const int64_t latencyNs = endNs - startNs;

    out << requestIdx << ","
        << batchIdx << ","
        << slotIdx << ","
        << args.concurrency << ","
        << args.textBytes << ","
        << args.mentionCount << ","
        << args.urlCount << ","
        << args.userCount << ","
        << args.seed << ","
        << startNs << ","
        << endNs << ","
        << latencyNs << ","
        << (ok ? 1 : 0) << ","
        << csvSafe(status) << "\n";
}

faabric::rpc::Task<int64_t> getStoredPostCount(
  snb::PostStorageService::Stub& storeStub,
  std::string& status)
{
    snb::Empty empty;
    faabric::rpc::ClientContext ctx;

    auto call = storeStub.AsyncGetStoredPostCount(&ctx, empty);
    auto result = co_await call;

    if (!result.ok()) {
        status = std::string(result.status().message());
        co_return -1;
    }

    status = "OK";
    co_return result.value().count();
}

faabric::rpc::Task<void> runBenchmark(const Args& args)
{
    printf("[Client] BENCHMARK_CLIENT_VERSION=per_request_csv_v1\n");
    printf("[Client] total=%d concurrency=%d text=%d mentions=%d urls=%d "
           "users=%d seed=%d warmup=%d verify_storage=%d\n",
           args.totalRequests,
           args.concurrency,
           args.textBytes,
           args.mentionCount,
           args.urlCount,
           args.userCount,
           args.seed,
           args.warmupRequests,
           args.verifyStorage ? 1 : 0);
    fflush(stdout);

    std::shared_ptr<faabric::rpc::Channel> composeChannel;

    if (auto s = faabric::rpc::CreateChannel(
          snb::ComposePostService::ServiceUri,
          &composeChannel);
        !s.ok()) {
        setErrorOutput("failed to connect to ComposePost: " +
                       std::string(s.message()));
        co_return;
    }

    std::shared_ptr<faabric::rpc::Channel> storeChannel;

    if (args.verifyStorage) {
        if (auto s = faabric::rpc::CreateChannel(
              snb::PostStorageService::ServiceUri,
              &storeChannel);
            !s.ok()) {
            setErrorOutput("failed to connect to PostStorage: " +
                           std::string(s.message()));
            co_return;
        }
    }

    auto composeStub = snb::ComposePostService::NewStub(composeChannel);

    std::unique_ptr<snb::PostStorageService::Stub> storeStub;
    if (args.verifyStorage) {
        storeStub = snb::PostStorageService::NewStub(storeChannel);
    }

    // ---------------------------------------------------------------------
    // Warmup. Not recorded in CSV.
    // ---------------------------------------------------------------------

    int warmupIssued = 0;

    while (warmupIssued < args.warmupRequests) {
        const int batchSize =
          std::min(args.concurrency, args.warmupRequests - warmupIssued);

        std::vector<faabric::rpc::ClientContext> ctxs(batchSize);
        std::vector<snb::ComposePostRequest> reqs(batchSize);

        using ComposeCall = faabric::rpc::RpcCall<snb::Empty>;
        std::vector<ComposeCall> calls;
        calls.reserve(batchSize);

        for (int i = 0; i < batchSize; i++) {
            const int warmupIdx = warmupIssued + i;
            const int64_t reqId = -1 - static_cast<int64_t>(warmupIdx);

            fillComposeRequest(reqs[i], args, warmupIdx, reqId);
            calls.push_back(composeStub->AsyncComposePost(&ctxs[i], reqs[i]));
        }

        for (int i = 0; i < batchSize; i++) {
            auto result = co_await calls[i];

            if (!result.ok()) {
                fprintf(stderr,
                        "[Client] Warmup request failed: %s\n",
                        std::string(result.status().message()).c_str());
            }
        }

        warmupIssued += batchSize;
    }

    // ---------------------------------------------------------------------
    // Optional storage baseline after warmup.
    // ---------------------------------------------------------------------

    int64_t initialStored = -1;
    int64_t finalStored = -1;
    std::string storageStatus = args.verifyStorage ? "unchecked" : "disabled";

    if (args.verifyStorage) {
        initialStored = co_await getStoredPostCount(*storeStub, storageStatus);
    }

    // ---------------------------------------------------------------------
    // Measured benchmark.
    // ---------------------------------------------------------------------

    std::ostringstream csv;
    csv << "# benchmark_client_version=per_request_csv_v1\n";
    appendCsvHeader(csv);

    int issued = 0;
    int batchIdx = 0;
    int successes = 0;
    int failures = 0;

    auto benchStart = std::chrono::steady_clock::now();

    while (issued < args.totalRequests) {
        const int batchSize =
          std::min(args.concurrency, args.totalRequests - issued);

        std::vector<faabric::rpc::ClientContext> ctxs(batchSize);
        std::vector<snb::ComposePostRequest> reqs(batchSize);
        std::vector<int64_t> startTimes(batchSize);
        std::vector<int> requestIdxs(batchSize);

        using ComposeCall = faabric::rpc::RpcCall<snb::Empty>;
        std::vector<ComposeCall> calls;
        calls.reserve(batchSize);

        // Dispatch the whole batch before awaiting any response.
        for (int i = 0; i < batchSize; i++) {
            const int requestIdx = issued + i;
            const int64_t reqId = static_cast<int64_t>(requestIdx);

            requestIdxs[i] = requestIdx;
            fillComposeRequest(reqs[i], args, requestIdx, reqId);

            const auto t0 = std::chrono::steady_clock::now();
            startTimes[i] = durationNs(t0 - benchStart);

            calls.push_back(composeStub->AsyncComposePost(&ctxs[i], reqs[i]));
        }

        // Await in index order. There are still up to `concurrency` calls
        // outstanding because they were all dispatched above.
        for (int i = 0; i < batchSize; i++) {
            auto result = co_await calls[i];

            const auto t1 = std::chrono::steady_clock::now();
            const int64_t endNs = durationNs(t1 - benchStart);

            bool ok = result.ok();
            std::string status = "OK";

            if (ok) {
                successes++;
            } else {
                failures++;
                status = std::string(result.status().message());
            }

            appendCsvRow(csv,
                         requestIdxs[i],
                         batchIdx,
                         i,
                         args,
                         startTimes[i],
                         endNs,
                         ok,
                         status);
        }

        issued += batchSize;
        batchIdx++;
    }

    if (args.verifyStorage) {
        finalStored = co_await getStoredPostCount(*storeStub, storageStatus);

        if (initialStored >= 0 && finalStored >= 0) {
            const int64_t delta = finalStored - initialStored;

            if (delta != successes) {
                storageStatus = "stored_count_delta_mismatch";
            }
        }
    }

    csv << "# summary,total_requests=" << args.totalRequests
        << ",concurrency=" << args.concurrency
        << ",text_bytes=" << args.textBytes
        << ",mention_count=" << args.mentionCount
        << ",url_count=" << args.urlCount
        << ",user_count=" << args.userCount
        << ",seed=" << args.seed
        << ",warmup_requests=" << args.warmupRequests
        << ",method=compose_post"
        << ",successes=" << successes
        << ",failures=" << failures
        << ",verify_storage=" << (args.verifyStorage ? 1 : 0)
        << ",initial_stored=" << initialStored
        << ",final_stored=" << finalStored
        << ",storage_delta="
        << ((initialStored >= 0 && finalStored >= 0)
              ? finalStored - initialStored
              : -1)
        << ",storage_status=" << csvSafe(storageStatus)
        << "\n";

    const std::string out = csv.str();
    setOutput(out);

    printf("[Client] Benchmark finished: successes=%d failures=%d "
           "output_size=%zu\n",
           successes,
           failures,
           out.size());
    fflush(stdout);

    co_return;
}

} // namespace snb

int main(int argc, char* argv[])
{
    snb::Args args = snb::parseArgs(argc, argv);

    auto task = snb::runBenchmark(args);

    while (task.resume()) {
    }

    task.promise().result();

    return 0;
}