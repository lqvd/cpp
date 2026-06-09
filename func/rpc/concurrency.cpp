#include <faasm/faasm.h>

#include <faasrpc/Channel.h>
#include <faasrpc/ClientContext.h>
#include <faasrpc/RpcCall.h>
#include <faasrpc/Status.h>
#include <faasrpc/Task.h>

#include <rpc.h>

#include "Bench.faabric.h"
#include "Bench.pb.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Args
{
    int totalRequests = 1000;
    int concurrency = 1;
    int payloadBytes = 64;
    bool useEcho = true;
};

// argv:
//   argv[1] totalRequests, default 1000
//   argv[2] concurrency,   default 1
//   argv[3] payloadBytes,  default 64
//   argv[4] method,        "echo" or "noop", default "echo"
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
        args.payloadBytes = std::max(0, std::atoi(argv[3]));
    }

    if (argc >= 5) {
        std::string method = argv[4];
        if (method == "noop") {
            args.useEcho = false;
        } else if (method == "echo") {
            args.useEcho = true;
        } else {
            printf("[WASM] Unknown method '%s', using echo\n", method.c_str());
        }
    }

    args.concurrency = std::min(args.concurrency, args.totalRequests);
    return args;
}

uint64_t nowNs()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(
             steady_clock::now().time_since_epoch())
      .count();
}

void appendCsvRow(std::ostringstream& out,
                  int requestIdx,
                  int batchIdx,
                  int slotIdx,
                  int concurrency,
                  int payloadBytes,
                  uint64_t startNs,
                  uint64_t endNs,
                  bool ok,
                  const std::string& status)
{
    out << requestIdx << ","
        << batchIdx << ","
        << slotIdx << ","
        << concurrency << ","
        << payloadBytes << ","
        << startNs << ","
        << endNs << ","
        << (endNs - startNs) << ","
        << (ok ? 1 : 0) << ","
        << status << "\n";
}

} // namespace

faabric::rpc::Task<void> runEchoBenchmark(
  std::shared_ptr<faabric::rpc::Channel> channel,
  const Args& args,
  std::string& output)
{
    rpc::BenchSvc::Stub stub(channel);

    std::ostringstream csv;
    csv << "request_idx,batch_idx,slot_idx,concurrency,payload_bytes,"
        << "start_ns,end_ns,latency_ns,ok,status\n";

    const std::string payload(args.payloadBytes, 'x');

    int issued = 0;
    int batchIdx = 0;
    int successes = 0;
    int failures = 0;

    const uint64_t benchStartNs = nowNs();

    while (issued < args.totalRequests) {
        const int batchSize =
          std::min(args.concurrency, args.totalRequests - issued);

        std::vector<faabric::rpc::ClientContext> ctxs(batchSize);
        std::vector<rpc::EchoRequest> reqs(batchSize);
        std::vector<uint64_t> startTimes(batchSize);
        std::vector<int> requestIdxs(batchSize);

        using EchoCall = faabric::rpc::RpcCall<rpc::EchoResponse>;
        std::vector<EchoCall> calls;
        calls.reserve(batchSize);

        // Dispatch the whole batch before awaiting any response.
        for (int i = 0; i < batchSize; i++) {
            const int requestIdx = issued + i;
            requestIdxs[i] = requestIdx;

            reqs[i].set_payload(payload);

            startTimes[i] = nowNs() - benchStartNs;
            calls.push_back(stub.AsyncEcho(&ctxs[i], reqs[i]));
        }

        // Await in index order. This keeps up to `concurrency` RPCs outstanding.
        for (int i = 0; i < batchSize; i++) {
            auto result = co_await calls[i];

            const uint64_t endNs = nowNs() - benchStartNs;

            bool ok = result.ok();
            std::string status = "OK";

            if (!ok) {
                status = std::string(result.status().message());
                failures++;
            } else if (result.value().payload() != payload) {
                ok = false;
                status = "payload_mismatch";
                failures++;
            } else {
                successes++;
            }

            appendCsvRow(csv,
                         requestIdxs[i],
                         batchIdx,
                         i,
                         args.concurrency,
                         args.payloadBytes,
                         startTimes[i],
                         endNs,
                         ok,
                         status);
        }

        issued += batchSize;
        batchIdx++;
    }

    csv << "# summary,total_requests=" << args.totalRequests
        << ",concurrency=" << args.concurrency
        << ",payload_bytes=" << args.payloadBytes
        << ",method=echo"
        << ",successes=" << successes
        << ",failures=" << failures
        << "\n";

    output = csv.str();
    co_return;
}

faabric::rpc::Task<void> runNoopBenchmark(
  std::shared_ptr<faabric::rpc::Channel> channel,
  const Args& args,
  std::string& output)
{
    rpc::BenchSvc::Stub stub(channel);

    std::ostringstream csv;
    csv << "request_idx,batch_idx,slot_idx,concurrency,payload_bytes,"
        << "start_ns,end_ns,latency_ns,ok,status\n";

    int issued = 0;
    int batchIdx = 0;
    int successes = 0;
    int failures = 0;

    const uint64_t benchStartNs = nowNs();

    while (issued < args.totalRequests) {
        const int batchSize =
          std::min(args.concurrency, args.totalRequests - issued);

        std::vector<faabric::rpc::ClientContext> ctxs(batchSize);
        std::vector<rpc::NoopRequest> reqs(batchSize);
        std::vector<uint64_t> startTimes(batchSize);
        std::vector<int> requestIdxs(batchSize);

        using NoopCall = faabric::rpc::RpcCall<rpc::NoopResponse>;
        std::vector<NoopCall> calls;
        calls.reserve(batchSize);

        for (int i = 0; i < batchSize; i++) {
            const int requestIdx = issued + i;
            requestIdxs[i] = requestIdx;

            startTimes[i] = nowNs() - benchStartNs;
            calls.push_back(stub.AsyncNoop(&ctxs[i], reqs[i]));
        }

        for (int i = 0; i < batchSize; i++) {
            auto result = co_await calls[i];

            const uint64_t endNs = nowNs() - benchStartNs;

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
                         args.payloadBytes,
                         startTimes[i],
                         endNs,
                         ok,
                         status);
        }

        issued += batchSize;
        batchIdx++;
    }

    csv << "# summary,total_requests=" << args.totalRequests
        << ",concurrency=" << args.concurrency
        << ",payload_bytes=" << args.payloadBytes
        << ",method=noop"
        << ",successes=" << successes
        << ",failures=" << failures
        << "\n";

    output = csv.str();
    co_return;
}

int main(int argc, char* argv[])
{
    Args args = parseArgs(argc, argv);

    printf("[WASM] Starting steady-state RPC benchmark: "
           "total=%d concurrency=%d payload=%d method=%s\n",
           args.totalRequests,
           args.concurrency,
           args.payloadBytes,
           args.useEcho ? "echo" : "noop");

    std::shared_ptr<faabric::rpc::Channel> channel;
    faabric::rpc::Status status =
      faabric::rpc::CreateChannel(rpc::BenchSvc::ServiceUri, &channel);

    if (!status.ok()) {
        std::string out = "request_idx,batch_idx,slot_idx,concurrency,"
                          "payload_bytes,start_ns,end_ns,latency_ns,ok,status\n";
        out += "# error,channel failed: ";
        out += std::string(status.message());
        out += "\n";

        faasmSetOutput(out.c_str(), static_cast<long>(out.size()));
        return 1;
    }

    std::string output;

    faabric::rpc::Task<void>* task = nullptr;
    if (args.useEcho) {
        task = new faabric::rpc::Task<void>(
          runEchoBenchmark(channel, args, output));
    } else {
        task = new faabric::rpc::Task<void>(
          runNoopBenchmark(channel, args, output));
    }

    task->resume();
    task->destroy();
    delete task;

    faasmSetOutput(output.c_str(), static_cast<long>(output.size()));

    printf("[WASM] Benchmark finished. output_size=%zu\n", output.size());
    return 0;
}