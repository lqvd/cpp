#include <faasrpc/ClientContext.h>
#include <faasrpc/ServerBuilder.h>
#include <faasrpc/Status.h>
#include <faasrpc/Channel.h>

#include "composePost/proto/compose_post.faabric.h"
#include "composePost/proto/compose_post.pb.h"
#include "user/proto/user.faabric.h"
#include "user/proto/user.pb.h"
#include "text/proto/text.faabric.h"
#include "text/proto/text.pb.h"
#include "unique_id/proto/unique_id.faabric.h"
#include "unique_id/proto/unique_id.pb.h"
#include "postStorage/proto/post_storage.faabric.h"
#include "postStorage/proto/post_storage.pb.h"

#include <chrono>
#include <cstdio>
#include <memory>

namespace snb {

class ComposePostServiceImpl final
  : public snb::ComposePostService::Service
{
  public:
    faabric::rpc::Task<faabric::rpc::Status> ComposePost(
        faabric::rpc::ServerContext* ctx,
        const snb::ComposePostRequest* req,
        snb::Empty* resp) override
    {
        if (!initialized_) {
            if (auto s = faabric::rpc::CreateChannel(
                    snb::UserService::ServiceUri,
                    &userChannel_); !s.ok()) co_return s;

            if (auto s = faabric::rpc::CreateChannel(
                    snb::TextService::ServiceUri,
                    &textChannel_); !s.ok()) co_return s;

            if (auto s = faabric::rpc::CreateChannel(
                    snb::UniqueIdService::ServiceUri,
                    &uidChannel_); !s.ok()) co_return s;

            if (auto s = faabric::rpc::CreateChannel(
                    snb::PostStorageService::ServiceUri,
                    &storeChannel_); !s.ok()) co_return s;

            userStub_  = snb::UserService::NewStub(userChannel_);
            textStub_  = snb::TextService::NewStub(textChannel_);
            uidStub_   = snb::UniqueIdService::NewStub(uidChannel_);
            storeStub_ = snb::PostStorageService::NewStub(storeChannel_);

            initialized_ = true;
        }

        (void)ctx;
        (void)resp;

        // --- build downstream requests ---
        snb::ComposeCreatorWithUserIdRequest creatorReq;
        creatorReq.set_req_id(req->req_id());
        creatorReq.set_user_id(req->user_id());
        creatorReq.set_username(req->username());

        snb::ComposeTextRequest textReq;
        textReq.set_req_id(req->req_id());
        textReq.set_text(req->text());

        snb::ComposeUniqueIdRequest uidReq;
        uidReq.set_req_id(req->req_id());
        uidReq.set_post_type(req->post_type());

        // --- fan-out ---
        faabric::rpc::ClientContext creatorCtx, textCtx, uidCtx;

        auto creatorCall = userStub_->AsyncComposeCreatorWithUserId(
            &creatorCtx, creatorReq);
        auto textCall    = textStub_->AsyncComposeText(
            &textCtx, textReq);
        auto uidCall     = uidStub_->AsyncComposeUniqueId(
            &uidCtx, uidReq);

        // --- fan-in ---
        auto creatorResult = co_await creatorCall;
        if (!creatorResult.ok()) {
            fprintf(stderr, "[ComposePost] UserService failed: %s\n",
                    creatorResult.status().message().data());
            co_return creatorResult.status();
        }

        auto textResult = co_await textCall;
        if (!textResult.ok()) {
            fprintf(stderr, "[ComposePost] TextService failed: %s\n",
                    textResult.status().message().data());
            co_return textResult.status();
        }

        auto uidResult = co_await uidCall;
        if (!uidResult.ok()) {
            fprintf(stderr, "[ComposePost] UniqueIdService failed: %s\n",
                    uidResult.status().message().data());
            co_return uidResult.status();
        }

        // --- assemble post ---
        snb::Post post;
        post.set_post_id(uidResult.value().id());
        post.set_req_id(req->req_id());
        post.set_post_type(req->post_type());
        post.set_text(textResult.value().text());
        post.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
            .count());
        *post.mutable_creator()       = creatorResult.value();
        *post.mutable_user_mentions() = textResult.value().user_mentions();
        *post.mutable_urls()          = textResult.value().urls();

        // --- store ---
        snb::StorePostRequest storeReq;
        storeReq.set_req_id(req->req_id());
        *storeReq.mutable_post() = std::move(post);

        faabric::rpc::ClientContext storeCtx;
        auto storeCall   = storeStub_->AsyncStorePost(&storeCtx, storeReq);
        auto storeResult = co_await storeCall;
        if (!storeResult.ok()) {
            fprintf(stderr, "[ComposePost] PostStorageService failed: %s\n",
                    storeResult.status().message().data());
            co_return storeResult.status();
        }

        co_return faabric::rpc::Status::OK();
    }

  private:
    bool initialized_ = false;

    std::shared_ptr<faabric::rpc::Channel> userChannel_;
    std::shared_ptr<faabric::rpc::Channel> textChannel_;
    std::shared_ptr<faabric::rpc::Channel> uidChannel_;
    std::shared_ptr<faabric::rpc::Channel> storeChannel_;

    std::unique_ptr<snb::UserService::Stub> userStub_;
    std::unique_ptr<snb::TextService::Stub> textStub_;
    std::unique_ptr<snb::UniqueIdService::Stub> uidStub_;
    std::unique_ptr<snb::PostStorageService::Stub> storeStub_;
};

} // namespace snb

int main()
{
    faabric::rpc::ServerBuilder()
        .registerService(std::make_unique<snb::ComposePostServiceImpl>())
        .buildAndStart();
    return 0;
}