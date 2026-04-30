#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Guest-side coroutine migration trampoline.
// Called by the receiving host executor on the after snapshot restore to resume
// a suspended coroutine from its frame offset.
void __faasm_rpc_coro_trampoline(int32_t frameOffset);

#ifdef __cplusplus
}
#endif


namespace faabric::coro {

// A helper function in the guest context to enable passing in the trampoline
// function as the entry point for Faasm migration.
inline int32_t coro_trampoline_index()
{
    return static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(&__faasm_rpc_coro_trampoline));
}

} // namespace faabric::coro