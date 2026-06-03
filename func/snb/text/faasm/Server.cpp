#include <faasrpc/ServerBuilder.h>
#include "text/proto/text.faabric.h"
#include "text/proto/text.pb.h"
#include "common/social_types.pb.h"

#include <cstdio>
#include <memory>
#include <regex>
#include <string>

namespace snb {

class TextServiceImpl final : public snb::TextService::Service
{
  public:
    faabric::rpc::Task<faabric::rpc::Status> ComposeText(
        faabric::rpc::ServerContext* ctx,
        const snb::ComposeTextRequest* req,
        snb::TextServiceReturn* res) override
    {
        (void)ctx;

        res->set_text(req->text());

        // Extract @mentions
        static const std::regex mentionRe(R"(@(\w+))");
        auto begin = std::sregex_iterator(
            req->text().begin(), req->text().end(), mentionRe);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            auto* mention = res->add_user_mentions();
            mention->set_username((*it)[1].str());
            mention->set_user_id(0); // not resolved at this layer
        }

        // Extract URLs
        static const std::regex urlRe(R"(https?://\S+)");
        begin = std::sregex_iterator(
            req->text().begin(), req->text().end(), urlRe);
        for (auto it = begin; it != end; ++it) {
            auto* url = res->add_urls();
            url->set_shortened_url((*it)[0].str());
            url->set_expanded_url((*it)[0].str());
        }

        co_return faabric::rpc::Status::OK();
    }
};

} // namespace snb

int main()
{
    faabric::rpc::ServerBuilder()
        .registerService(std::make_unique<snb::TextServiceImpl>())
        .buildAndStart();
    return 0;
}