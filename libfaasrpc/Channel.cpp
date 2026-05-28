#include <faasrpc/Channel.h>

#include <faabric/rpc/rpc.h>

#include <memory>
#include <string>
#include <utility>

namespace faabric::rpc {

Channel::Channel(ChannelId id, std::string targetUri)
  : channelId(id)
  , target(std::move(targetUri))
{}

Channel::~Channel()
{
    // Destructors cannot report errors sensibly.
    (void)close();
}

ChannelId Channel::id() const noexcept
{
    return channelId;
}

std::string_view Channel::targetUri() const noexcept
{
    return target;
}

bool Channel::valid() const noexcept
{
    return channelId != INVALID_CHANNEL_ID;
}

Status Channel::close() noexcept
{
    if (!valid()) {
        return Status::OK();
    }

    ChannelId id = channelId;
    channelId = INVALID_CHANNEL_ID;

    return CloseChannelId(id);
}

Status CreateChannelId(std::string_view targetUri, ChannelId* out)
{
    if (out == nullptr) {
        return Status{
            Rpc_StatusCode::INVALID_ARGUMENT,
            "Null output pointer passed to CreateChannelId"
        };
    }

    *out = -1;

    if (targetUri.empty()) {
        return Status{
            Rpc_StatusCode::INVALID_ARGUMENT,
            "Empty RPC target URI"
        };
    }

    // string_view is not guaranteed to be null-terminated.
    std::string uri(targetUri);

    ChannelId id = -1;
    int32_t statusCode = __faasm_rpc_channel_create(uri.c_str(), &id);

    if (statusCode != Rpc_StatusCode::OK) {
        return Status{
            statusCode,
            "Failed to create RPC channel"
        };
    }

    *out = id;
    return Status::OK();
}

Status CloseChannelId(ChannelId channelId) noexcept
{
    if (channelId < 0) {
        return Status{
            Rpc_StatusCode::INVALID_ARGUMENT,
            "Invalid RPC channel id"
        };
    }

    int32_t statusCode = __faasm_rpc_channel_close(channelId);

    if (statusCode != Rpc_StatusCode::OK) {
        return Status{
            statusCode,
            "Failed to close RPC channel"
        };
    }

    return Status::OK();
}

Status CreateChannel(std::string_view targetUri,
                     std::shared_ptr<Channel>* out)
{
    if (out == nullptr) {
        return Status{
            Rpc_StatusCode::INVALID_ARGUMENT,
            "Null output pointer passed to CreateChannel"
        };
    }

    out->reset();

    ChannelId id = -1;
    Status status = CreateChannelId(targetUri, &id);

    if (!status.ok()) {
        return status;
    }

    *out = std::shared_ptr<Channel>(
      new Channel(id, std::string(targetUri)));

    return Status::OK();
}

} // namespace faabric::rpc