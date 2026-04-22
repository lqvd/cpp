set(FAASM_USER rpc)

set(ORPC_CXX ${CXX})
set(ORPC_C ${C})

function(rpc_func exec_name dir_path)
    faasm_func(${exec_name} ${dir_path})

    target_link_libraries(${exec_name} faasrpc rpc)

    if (NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
        target_link_libraries(${exec_name} pthread)
    endif ()

    # Add to list of all funcs
    set(ALL_RPC_FUNCS ${ALL_RPC_FUNCS} ${exec_name} PARENT_SCOPE)
endfunction(rpc_func)

rpc_func(migrate_rpc migrate.cpp)

# Custom target for building all functions
add_custom_target(rpc_all_funcs DEPENDS ${ALL_RPC_FUNCS})
