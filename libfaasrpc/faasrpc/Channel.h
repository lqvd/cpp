#pragma once

#include <faabric/rpc/rpc.h>
#include <faasrpc/Status.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace faabric::rpc {

using ChannelId = int32_t;

class Channel final
{
  public:
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    Channel(Channel&&) = delete;
    Channel& operator=(Channel&&) = delete;

    ~Channel();

    ChannelId id() const noexcept;
    std::string_view targetUri() const noexcept;
    bool valid() const noexcept;

    // Manual close, but also closed on destruction!
    faabric::rpc::Status close() noexcept;

  private:
    friend faabric::rpc::Status CreateChannel(std::string_view targetUri,
                                std::shared_ptr<Channel>* out);

    Channel(ChannelId id, std::string targetUri);

    static constexpr ChannelId INVALID_CHANNEL_ID = -1;

    ChannelId channelId = INVALID_CHANNEL_ID;
    std::string target;
};

// Main ergonomic API.
faabric::rpc::Status CreateChannel(std::string_view targetUri,
                     std::shared_ptr<Channel>* out);

// Thin low-level wrappers around the private ABI.
faabric::rpc::Status CreateChannelId(std::string_view targetUri, ChannelId* out);
faabric::rpc::Status CloseChannelId(ChannelId channelId) noexcept;

}