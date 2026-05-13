#pragma once

#include <faasm/host_interface.h>
#include <faasrpc/coro_trampoline.h>

#include <coroutine>
#include <cstdint>

namespace faabric::rpc {

// A lightweight coroutine which acts as a migration checkpoint. Checks for and
// performs migration, otherwise continues the coroutine.
struct MigrationCheckpoint {
    bool await_ready() noexcept { return false; }

    bool await_suspend(std::coroutine_handle<> h) noexcept {
        int32_t frameOffset = static_cast<int32_t>(
            reinterpret_cast<uintptr_t>(h.address()));
        __faasm_migrate_point(&__faasm_rpc_coro_trampoline, frameOffset);
        return false;
    }

    void await_resume() noexcept {}
};

} // namespace faabric::rpc