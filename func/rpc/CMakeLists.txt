set(FAASM_USER rpc)

add_subdirectory(common)

function(rpc_func exec_name dir_path)
    faasm_func(${exec_name} ${dir_path})

    target_link_libraries(${exec_name} faasrpc rpc rpc_dist_test_common)

    if (NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
        target_link_libraries(${exec_name} pthread)
    endif ()

    # Add to list of all funcs
    set(ALL_RPC_FUNCS ${ALL_RPC_FUNCS} ${exec_name} PARENT_SCOPE)

    target_compile_options(${exec_name} PRIVATE -std=gnu++20)
    target_compile_features(${exec_name} PRIVATE cxx_std_20)
endfunction(rpc_func)

rpc_func(migrate_rpc migrate.cpp)
rpc_func(coro coroutine_layout.cpp)
rpc_func(rpc_forwarding rpc_forwarding.cpp)
rpc_func(rpc_pending rpc_pending.cpp)
rpc_func(rpc_cached rpc_cached.cpp)

# Custom target for building all functions
add_custom_target(rpc_all_funcs DEPENDS ${ALL_RPC_FUNCS})