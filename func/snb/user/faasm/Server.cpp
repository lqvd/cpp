#include <faasrpc/ServerBuilder.h>
#include <faasrpc/ClientContext.h>
#include "user/proto/user.faabric.h"
#include "user/proto/user.pb.h"
#include "user_db/proto/user_db.faabric.h"
#include "user_db/proto/user_db.pb.h"
#include "common/social_types.pb.h"

#include <cstdio>
#include <memory>
#include <string>

namespace snb {

class UserServiceImpl final : public snb::UserService::Service
{
  public:
    faabric::rpc::Task<faabric::rpc::Status> ComposeCreatorWithUserId(
        faabric::rpc::ServerContext* ctx,
        const snb::ComposeCreatorWithUserIdRequest* req,
        snb::Creator* res) override
    {
        if (!initialized_) {
            if (auto s = faabric::rpc::CreateChannel(
                snb::UserDbService::ServiceUri,
                &dbChannel_); !s.ok()) co_return s;

            dbStub_ = snb::UserDbService::NewStub(dbChannel_);

            initialized_ = true;
        }

        (void)ctx;

        snb::GetUserRequest dbReq;
        dbReq.set_user_id(req->user_id());

        faabric::rpc::ClientContext dbCtx;
        auto dbCall   = dbStub_->AsyncGetUser(&dbCtx, dbReq);
        auto dbResult = co_await dbCall;

        if (!dbResult.ok()) {
            fprintf(stderr, "[UserService] DB lookup failed: %s\n",
                    dbResult.status().message().data());
            co_return dbResult.status();
        }

        res->set_user_id(req->user_id());
        res->set_username(dbResult.value().username());

        co_return faabric::rpc::Status::OK();
    }

  private:
    bool initialized_ = false;

    std::shared_ptr<faabric::rpc::Channel> dbChannel_;
    std::unique_ptr<snb::UserDbService::Stub> dbStub_;
};

} // namespace snb

int main()
{
    faabric::rpc::ServerBuilder()
        .registerService(std::make_unique<snb::UserServiceImpl>())
        .buildAndStart();
    return 0;
}