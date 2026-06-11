#pragma once
#include <coroutine>

namespace faabric::rpc {

// Parks the coroutine unconditionally. Used by the server loop to return
// control to the driver once per request, unwinding the per-iteration
// resume chain (Task transfers are plain nested calls on wasm32 without
// tail calls, so the loop must suspend to release them).
struct YieldToDriver
{
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
};

} // namespace faabric::rpc