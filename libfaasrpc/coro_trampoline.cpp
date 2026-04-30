#include "faasrpc/coro_trampoline.h"
#include <coroutine>
#include <cstdint>

extern "C" {

// Called by host B's executor after snapshot restore (from A).
// frameOffset is a Wasm linear memory offset — guest address space
// starts at 0, so the offset is the address directly.
__attribute__((export_name("__faasm_rpc_coro_trampoline")))
void __faasm_rpc_coro_trampoline(int32_t frameOffset)
{
    void* framePtr = reinterpret_cast<void*>(
        static_cast<uintptr_t>(frameOffset));
    auto handle = std::coroutine_handle<>::from_address(framePtr);
    handle.resume();
}

} // extern "C"