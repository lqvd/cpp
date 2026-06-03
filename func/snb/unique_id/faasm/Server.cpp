#include <faasrpc/ServerBuilder.h>
#include "unique_id/proto/unique_id.faabric.h"
#include "unique_id/proto/unique_id.pb.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>

namespace snb {

class UniqueIdServiceImpl final : public snb::UniqueIdService::Service
{
  public:
    faabric::rpc::Task<faabric::rpc::Status> ComposeUniqueId(
        faabric::rpc::ServerContext* ctx,
        const snb::ComposeUniqueIdRequest* req,
        snb::ComposeUniqueIdResponse* res) override
    {
        (void)ctx;

        // Combine timestamp + counter for a unique id
        // Timestamp in ms shifted left, counter in low bits
        // Gives uniqueness within a single service instance
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

        uint64_t counter = counter_.fetch_add(1, std::memory_order_relaxed);
        int64_t id = (now << 20) | (counter & 0xFFFFF);

        res->set_id(id);

        co_return faabric::rpc::Status::OK();
    }

  private:
    std::atomic<uint64_t> counter_{ 0 };
};

} // namespace snb

int main()
{
    faabric::rpc::ServerBuilder()
        .registerService(std::make_unique<snb::UniqueIdServiceImpl>())
        .buildAndStart();
    return 0;
}