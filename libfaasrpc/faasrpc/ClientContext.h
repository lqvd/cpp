#pragma once

#include <cstdint>

namespace faabric::rpc {

class ClientContext
{
  public:
    ClientContext() = default;

    void setTimeoutMs(int32_t timeoutMsIn)
    {
        timeoutMs_ = timeoutMsIn;
    }

    int32_t getTimeoutMs() const noexcept
    {
        return timeoutMs_;
    }

  private:
    int32_t timeoutMs_ = -1;
};

} // namespace faabric::rpc