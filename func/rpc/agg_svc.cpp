#include <faasrpc/ClientContext.h>
#include <faasrpc/ServerBuilder.h>
#include <faasrpc/Status.h>

#include "Aggregator.faabric.h"
#include "Aggregator.pb.h"
#include "Ping.faabric.h"
#include "Ping.pb.h"

#include <cstdio>
#include <memory>
#include <string>

class AggregatorSvcImpl final : public rpc::AggregatorSvc::Service
{
  public:
    faabric::rpc::Task<faabric::rpc::Status> Aggregate(
      faabric::rpc::ServerContext* ctx,
      const rpc::AggregateRequest* req,
      rpc::AggregateResponse* resp) override
    {
        (void)ctx;

        printf("[AggregatorSvc] Aggregate request: '%s'\n",
               req->message().c_str());

        std::unique_ptr<rpc::PingSvc::Stub> pingStub;
        faabric::rpc::Status status = rpc::PingSvc::NewStub(&pingStub);

        if (!status.ok()) {
            resp->set_message(std::string(status.message()));
            co_return status;
        }

        const std::string base = req->message();

        rpc::PingRequest reqA;
        reqA.set_message("agg-a: " + base);

        rpc::PingRequest reqB;
        reqB.set_message("agg-b: " + base);

        faabric::rpc::ClientContext ctxA;
        faabric::rpc::ClientContext ctxB;

        printf("[AggregatorSvc] Dispatching fan-out RPCs\n");

        auto pingA = pingStub->PingAsync(&ctxA, reqA);
        auto pingB = pingStub->PingAsync(&ctxB, reqB);

        printf("[AggregatorSvc] Awaiting ping A\n");
        auto resultA = co_await pingA;
        if (!resultA.ok()) {
            resp->set_message(std::string(resultA.status().message()));
            co_return resultA.status();
        }

        printf("[AggregatorSvc] Awaiting ping B\n");
        auto resultB = co_await pingB;
        if (!resultB.ok()) {
            resp->set_message(std::string(resultB.status().message()));
            co_return resultB.status();
        }

        const rpc::PingResponse& respA = resultA.value();
        const rpc::PingResponse& respB = resultB.value();

        printf("[AggregatorSvc] Fan-out complete: A='%s' B='%s'\n",
               respA.message().c_str(),
               respB.message().c_str());

        resp->set_message(
          "Agg: " + respA.message() + " | " + respB.message());

        co_return faabric::rpc::Status::OK();
    }
};

int main()
{
    faabric::rpc::ServerBuilder()
      .registerService(std::make_unique<AggregatorSvcImpl>())
      .buildAndStart();

    return 0;
}