#include <faasrpc/ServerBuilder.h>
#include "postStorage/proto/post_storage.faabric.h"
#include "postStorage/proto/post_storage.pb.h"
#include "common/social_types.pb.h"

#include <cstdio>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace snb {

class PostStorageServiceImpl final : public snb::PostStorageService::Service
{
  public:
    faabric::rpc::Task<faabric::rpc::Status> StorePost(
        faabric::rpc::ServerContext* ctx,
        const snb::StorePostRequest* req,
        snb::Empty* resp) override
    {
        (void)ctx;
        (void)resp;

        if (!req->has_post()) {
            co_return faabric::rpc::Status{
                Rpc_StatusCode::INVALID_ARGUMENT,
                "StorePost: missing post"};
        }

        const int64_t postId = req->post().post_id();

        {
            std::unique_lock lock(mx_);
            posts_.emplace(postId, req->post());
        }

        fprintf(stderr, "[PostStorage] Stored post_id=%lld req_id=%lld\n",
                        (long long)postId,
                        (long long)req->req_id());

        co_return faabric::rpc::Status::OK();
    }

    faabric::rpc::Task<faabric::rpc::Status> GetStoredPostCount(
      faabric::rpc::ServerContext* ctx,
      const snb::Empty* req,
      snb::StoredPostCount* res) override
    {
        res->set_count(posts_.size());
        co_return faabric::rpc::Status::OK();
    }

  private:
    mutable std::shared_mutex mx_;
    std::unordered_map<int64_t, snb::Post> posts_;
};

} // namespace snb

int main()
{
    faabric::rpc::ServerBuilder()
        .registerService(std::make_unique<snb::PostStorageServiceImpl>())
        .buildAndStart();
    return 0;
}