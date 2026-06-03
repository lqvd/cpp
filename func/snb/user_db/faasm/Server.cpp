#include <rpc.h>
#include <faasrpc/ServerBuilder.h>
#include <faasrpc/ClientContext.h>
#include "user/proto/user.faabric.h"
#include "user/proto/user.pb.h"
#include "user_db/proto/user_db.faabric.h"
#include "user_db/proto/user_db.pb.h"
#include "common/social_types.pb.h"

#include <coroutine>
#include <cstdio>
#include <memory>
#include <shared_mutex>
#include <string>

class UserDbServiceImpl final : public snb::UserDbService::Service
{
  public:
    UserDbServiceImpl()
    {
        for (int64_t i = 1; i <= 1000; ++i) {
            users_[i] = "user_" + std::to_string(i);
        }
    }

    faabric::rpc::Task<faabric::rpc::Status> GetUser(
        faabric::rpc::ServerContext* ctx,
        const snb::GetUserRequest* req,
        snb::GetUserResponse* res) override
    {
        (void)ctx;

        std::shared_lock lock(mx_);
        auto it = users_.find(req->user_id());
        if (it == users_.end()) {
            co_return faabric::rpc::Status{
                Rpc_StatusCode::NOT_FOUND,
                "User not found: " + std::to_string(req->user_id())};
        }

        res->set_username(it->second);
        co_return faabric::rpc::Status::OK();
    }

  private:
    mutable std::shared_mutex mx_;
    std::unordered_map<int64_t, std::string> users_;
};

int main()
{
    faabric::rpc::ServerBuilder()
        .registerService(std::make_unique<UserDbServiceImpl>())
        .buildAndStart();
    return 0;
}