#include <faasrpc/ServerBuilder.h>
#include <rpc.h>

#include "Bench.faabric.h"
#include "Bench.pb.h"

#include <memory>
#include <string>

class BenchSvcImpl final : public rpc::BenchSvc::Service
{
  public:
    faabric::rpc::Task<faabric::rpc::Status> Noop(
      faabric::rpc::ServerContext* ctx,
      const rpc::NoopRequest* req,
      rpc::NoopResponse* resp) override
    {
        // Deliberately empty: measures baseline RPC path.
        co_return faabric::rpc::Status{ Rpc_StatusCode::OK, "" };
    }

    faabric::rpc::Task<faabric::rpc::Status> Echo(
      faabric::rpc::ServerContext* ctx,
      const rpc::EchoRequest* req,
      rpc::EchoResponse* resp) override
    {
        // Echo exactly the payload so request/response sizes are controlled.
        resp->set_payload(req->payload());
        co_return faabric::rpc::Status{ Rpc_StatusCode::OK, "" };
    }
};

int main()
{
    faabric::rpc::ServerBuilder()
      .registerService(std::make_unique<BenchSvcImpl>())
      .buildAndStart();

    return 0;
}