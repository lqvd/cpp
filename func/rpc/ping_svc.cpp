#include <faasrpc/ServerBuilder.h>
#include <rpc.h>

#include "Ping.faabric.h"
#include "Ping.pb.h"

#include <memory>

class PingSvcImpl final : public rpc::PingSvc::Service
{
  public:
    Rpc_Status Ping(faabric::rpc::ServerContext* ctx,
                    const rpc::PingRequest* req,
                    rpc::PingResponse* resp) override
    {
        resp->set_message("Pong: " + req->message());
        return { Rpc_StatusCode::OK, "" };
    }
};

int main()
{
    faabric::rpc::ServerBuilder()
      .registerService(std::make_unique<PingSvcImpl>())
      .buildAndStart();

    return 0;
}