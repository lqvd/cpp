#include <faasm/faasm.h>
#include <faasm/time.h>

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
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

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

// argv:
//   argv[1] totalRequests,   default 1000
//   argv[2] concurrency,     default 1
//   argv[3] textBytes,       default 128
//   argv[4] mentionCount,    default 2
//   argv[5] urlCount,        default 1
//   argv[6] userCount,       default 100
//   argv[7] seed,            default 1
//   argv[8] warmupRequests,  default 100
//   argv[9] verifyStorage,   "0" or "1", default 0
Args parseArgs(int argc, char* argv[])
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

int64_t relNsSince(double benchStartSec)
{
    const double nowSec = faasm::getSecondsSinceEpoch();
    const double relSec = nowSec - benchStartSec;
    return static_cast<int64_t>(std::llround(relSec * 1e9));
}

std::string csvSafe(std::string s)
{
    for (char& c : s) {
        if (c == ',' || c == '\n' || c == '\r') {
            c = '_';
        }
    }

    return s;
}

void appendCsvHeader(std::ostringstream& out)
{
    out << "request_idx,batch_idx,slot_idx,concurrency,text_bytes,"
        << "mention_count,url_count,user_count,seed,"
        << "start_ns,end_ns,latency_ns,ok,status\n";
}

std::string makeErrorCsv(const std::string& message)
{
    std::ostringstream out;
    appendCsvHeader(out);
    out << "# error," << csvSafe(message) << "\n";
    return out.str();
}

void setErrorOutput(const std::string& message)
{
    const std::string s = makeErrorCsv(message);
    faasmSetOutput(s.c_str(), static_cast<long>(s.size()));
}

uint64_t mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

int boundedInt(const Args& args, int requestIdx, int stream, int upper)
{
    const uint64_t x =
      mix64(static_cast<uint64_t>(requestIdx) ^
            (static_cast<uint64_t>(args.seed) << 32) ^
            static_cast<uint64_t>(stream));

    return static_cast<int>(x % static_cast<uint64_t>(upper));
}

int64_t userIdForRequest(const Args& args, int requestIdx)
{
    return 1 + boundedInt(args, requestIdx, 0, args.userCount);
}

std::string usernameForUser(int64_t userId)
{
    return "user" + std::to_string(userId);
}

std::string makeText(const Args& args, int requestIdx)
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

void fillComposeRequest(snb::ComposePostRequest& req,
                        const Args& args,
                        int requestIdx,
                        int64_t reqId)
{
    const int64_t userId = userIdForRequest(args, requestIdx);

    req.set_req_id(reqId);
    req.set_user_id(userId);
    req.set_username(usernameForUser(userId));
    req.set_text(makeText(args, requestIdx));
    req.set_post_type(snb::POST);
}

void appendCsvRow(std::ostringstream& out,
                  int requestIdx,
                  int batchIdx,
                  int slotIdx,
                  int concurrency,
                  int textBytes,
                  int mentionCount,
                  int urlCount,
                  int userCount,
                  int seed,
                  int64_t startNs,
                  int64_t endNs,
                  bool ok,
                  const std::string& status)
{
    const int64_t latencyNs = endNs - startNs;

    out << requestIdx << ","
        << batchIdx << ","
        << slotIdx << ","
        << concurrency << ","
        << textBytes << ","
        << mentionCount << ","
        << urlCount << ","
        << userCount << ","
        << seed << ","
        << startNs << ","
        << endNs << ","
        << latencyNs << ","
        << (ok ? 1 : 0) << ","
        << csvSafe(status) << "\n";
}

} // namespace

faabric::rpc::Task<int64_t> getStoredPostCount(
  snb::PostStorageService::Stub& storageStub,
  std::string& status)
{
    faabric::rpc::ClientContext ctx;
    snb::Empty req;

    auto result = co_await storageStub.AsyncGetStoredPostCount(&ctx, req);

    if (!result.ok()) {
        status = std::string(result.status().message());
        co_return -1;
    }

    status = "OK";
    co_return result.value().count();
}

faabric::rpc::Task<void> runComposePostBenchmark(
  std::shared_ptr<faabric::rpc::Channel> composeChannel,
  std::shared_ptr<faabric::rpc::Channel> storageChannel,
  const Args& args,
  std::string& output,
  std::atomic<bool>& done)
{
    try {
        snb::ComposePostService::Stub composeStub(composeChannel);

        std::unique_ptr<snb::PostStorageService::Stub> storageStub;
        if (args.verifyStorage) {
            storageStub =
              std::make_unique<snb::PostStorageService::Stub>(storageChannel);
        }

        // Warmup is deliberately not recorded in the CSV.
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
                calls.push_back(
                  composeStub.AsyncComposePost(&ctxs[i], reqs[i]));
            }

            for (int i = 0; i < batchSize; i++) {
                auto result = co_await calls[i];

                if (!result.ok()) {
                    fprintf(stderr,
                            "[WASM] Warmup request failed: %s\n",
                            result.status().message().data());
                }
            }

            warmupIssued += batchSize;
        }

        int64_t initialStored = -1;
        int64_t finalStored = -1;
        std::string storageStatus =
          args.verifyStorage ? "unchecked" : "disabled";

        if (args.verifyStorage) {
            initialStored =
              co_await getStoredPostCount(*storageStub, storageStatus);
        }

        std::ostringstream csv;
        appendCsvHeader(csv);

        int issued = 0;
        int batchIdx = 0;
        int successes = 0;
        int failures = 0;

        const double benchStartSec = faasm::getSecondsSinceEpoch();

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
                requestIdxs[i] = requestIdx;

                const int64_t reqId = static_cast<int64_t>(requestIdx);

                fillComposeRequest(reqs[i], args, requestIdx, reqId);

                startTimes[i] = relNsSince(benchStartSec);
                calls.push_back(
                  composeStub.AsyncComposePost(&ctxs[i], reqs[i]));
            }

            // Await in index order. This keeps up to `concurrency` RPCs
            // outstanding.
            for (int i = 0; i < batchSize; i++) {
                auto result = co_await calls[i];

                const int64_t endNs = relNsSince(benchStartSec);

                bool ok = result.ok();
                std::string status = "OK";

                if (!ok) {
                    status = std::string(result.status().message());
                    failures++;
                } else {
                    successes++;
                }

                appendCsvRow(csv,
                             requestIdxs[i],
                             batchIdx,
                             i,
                             args.concurrency,
                             args.textBytes,
                             args.mentionCount,
                             args.urlCount,
                             args.userCount,
                             args.seed,
                             startTimes[i],
                             endNs,
                             ok,
                             status);
            }

            issued += batchSize;
            batchIdx++;
        }

        if (args.verifyStorage) {
            finalStored =
              co_await getStoredPostCount(*storageStub, storageStatus);

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

        output = csv.str();
    } catch (const std::exception& e) {
        output = makeErrorCsv(std::string("benchmark exception: ") + e.what());
    } catch (...) {
        output = makeErrorCsv("benchmark unknown exception");
    }

    done.store(true, std::memory_order_release);
    co_return;
}

int main(int argc, char* argv[])
{
    Args args = parseArgs(argc, argv);

    printf("[WASM] Starting SocialNetworkBench ComposePost benchmark: "
           "total=%d concurrency=%d text=%d mentions=%d urls=%d "
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
    faabric::rpc::Status status =
      faabric::rpc::CreateChannel(snb::ComposePostService::ServiceUri,
                                  &composeChannel);

    if (!status.ok()) {
        std::string msg = "compose channel failed: ";
        msg += std::string(status.message());
        setErrorOutput(msg);
        return 1;
    }

    std::shared_ptr<faabric::rpc::Channel> storageChannel;

    if (args.verifyStorage) {
        status = faabric::rpc::CreateChannel(
          snb::PostStorageService::ServiceUri,
          &storageChannel);

        if (!status.ok()) {
            std::string msg = "post-storage channel failed: ";
            msg += std::string(status.message());
            setErrorOutput(msg);
            return 1;
        }
    }

    std::string output;
    std::atomic<bool> done{ false };

    auto task = runComposePostBenchmark(
      composeChannel,
      storageChannel,
      args,
      output,
      done);

    task.resume();

    while (!done.load(std::memory_order_acquire)) {
        usleep(1000);
    }

    // Keep this if your Task<T> requires explicit destruction.
    // If Task<T>'s destructor already destroys the coroutine handle, remove this.
    task.destroy();

    if (output.empty()) {
        setErrorOutput("benchmark completed but produced empty output");
        return 1;
    }

    faasmSetOutput(output.c_str(), static_cast<long>(output.size()));

    printf("[WASM] SocialNetworkBench benchmark finished. output_size=%zu\n",
           output.size());
    fflush(stdout);

    return 0;
}