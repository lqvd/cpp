#include <faasrpc/ServerBuilder.h>
#include <rpc.h>

#include "PingSlow.faabric.h"
#include "PingSlow.pb.h"

#include <chrono>
#include <memory>
#include <string>
#include <thread>

class PingSlowSvcImpl final : public rpc::PingSlowSvc::Service
{
  public:
    faabric::rpc::Task<faabric::rpc::Status> Ping(
      faabric::rpc::ServerContext* ctx,
      const rpc::PingRequest* req,
      rpc::PingResponse* resp) override
    {
        // Make the callee slow enough that the caller remains suspended in
        // RpcCall::await_resume / pending RpcContext state during migration.
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));

        resp->set_message("Slow Pong: " + req->message());

        co_return faabric::rpc::Status{ Rpc_StatusCode::OK, "" };
    }
};

int main()
{
    faabric::rpc::ServerBuilder()
      .registerService(std::make_unique<PingSlowSvcImpl>())
      .buildAndStart();

    return 0;
}