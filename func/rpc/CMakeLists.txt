set(FAASM_USER rpc)

option(FAASRPC_ENABLE_MIGRATION
       "Enable migration-aware RPC paths"
       ON)

# --------------
# Protobuf / Faabric stub generation
# --------------
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

file(MAKE_DIRECTORY "${GENERATED_DIR}")

set(PLUGIN_BIN "${CMAKE_SOURCE_DIR}/../build/protogen-faabric/protoc-gen-faabric")

file(GLOB RPC_PROTOS "${PROTO_DIR}/*.proto")

set(GENERATED_PB_SRCS "")
set(ALL_GENERATED "")

foreach(PROTO_FILE IN LISTS RPC_PROTOS)
    get_filename_component(PROTO_FN "${PROTO_FILE}" NAME_WE)

    set(GEN_PB_CC "${GENERATED_DIR}/${PROTO_FN}.pb.cc")
    set(GEN_PB_H "${GENERATED_DIR}/${PROTO_FN}.pb.h")
    set(GEN_FAABRIC_H "${GENERATED_DIR}/${PROTO_FN}.faabric.h")

    add_custom_command(
        OUTPUT
            "${GEN_PB_CC}"
            "${GEN_PB_H}"
            "${GEN_FAABRIC_H}"
        COMMAND protoc
        ARGS
            "--plugin=protoc-gen-faabric=${PLUGIN_BIN}"
            "--faabric_out=${GENERATED_DIR}"
            "--cpp_out=${GENERATED_DIR}"
            -I "${PROTO_DIR}"
            "${PROTO_FILE}"
        DEPENDS
            "${PROTO_FILE}"
            "${PLUGIN_BIN}"
        COMMENT "Generating RPC stubs for ${PROTO_FN}"
        VERBATIM
    )

    list(APPEND GENERATED_PB_SRCS "${GEN_PB_CC}")
    list(APPEND ALL_GENERATED
        "${GEN_PB_CC}"
        "${GEN_PB_H}"
        "${GEN_FAABRIC_H}"
    )
endforeach()

add_custom_target(generate_proto_stubs DEPENDS ${ALL_GENERATED})

# --------------
# Generated protobuf message library
# --------------
add_library(rpc_generated_pb STATIC ${GENERATED_PB_SRCS})

add_dependencies(rpc_generated_pb generate_proto_stubs)

target_include_directories(rpc_generated_pb PUBLIC
    "${GENERATED_DIR}"
)

# --------------
# Protobuf runtime linkage
# --------------
if (CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(FAASM_WASM_SYSROOT "$ENV{WASM_SYSROOT}")

    if ("${FAASM_WASM_SYSROOT}" STREQUAL "")
        set(FAASM_WASM_SYSROOT "/usr/local/faasm/llvm-sysroot")
    endif()

    set(WASM_PROTOBUF_LITE
        "${FAASM_WASM_SYSROOT}/lib/libprotobuf-lite.a"
    )

    if (NOT EXISTS "${WASM_PROTOBUF_LITE}")
        message(FATAL_ERROR
            "Could not find WASM protobuf-lite at ${WASM_PROTOBUF_LITE}. "
            "Run inv libprotobuf.build first."
        )
    endif()

    target_include_directories(rpc_generated_pb PUBLIC
        "${FAASM_WASM_SYSROOT}/include"
    )

    target_link_libraries(rpc_generated_pb PUBLIC
        "${WASM_PROTOBUF_LITE}"
    )
else()
    find_package(Protobuf REQUIRED)

    target_include_directories(rpc_generated_pb PUBLIC
        ${Protobuf_INCLUDE_DIRS}
    )

    target_link_libraries(rpc_generated_pb PUBLIC
        ${Protobuf_LIBRARIES}
    )
endif()

# --------------
# Header-only generated Faabric RPC stubs
# --------------
add_library(rpc_generated_stubs INTERFACE)

add_dependencies(rpc_generated_stubs generate_proto_stubs)

target_include_directories(rpc_generated_stubs INTERFACE
    "${GENERATED_DIR}"
)

# --------------
# RPC functions
# --------------
function(rpc_func exec_name source_file)
    faasm_func(${exec_name} ${source_file})

    target_link_libraries(${exec_name}
        faasrpc
        rpc
        rpc_generated_stubs
        rpc_generated_pb
    )

    target_compile_definitions(${exec_name} PRIVATE
        FAASRPC_ENABLE_MIGRATION=$<BOOL:${FAASRPC_ENABLE_MIGRATION}>
    )

    if (NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
        target_link_libraries(${exec_name}
            pthread
        )
    endif()

    target_compile_options(${exec_name} PRIVATE
        -std=gnu++20
    )

    target_compile_features(${exec_name} PRIVATE
        cxx_std_20
    )

    set(ALL_RPC_FUNCS ${ALL_RPC_FUNCS} ${exec_name} PARENT_SCOPE)
endfunction()

rpc_func(AggSvc agg_svc.cpp)
rpc_func(BenchSvc bench_svc.cpp)
rpc_func(SteadyStateBench concurrency.cpp)
rpc_func(PingSvc ping_svc.cpp)
rpc_func(migrate_rpc migrate.cpp)
rpc_func(StressTest stress-test.cpp)
# rpc_func(coro coroutine_layout.cpp)

add_custom_target(rpc_all_funcs DEPENDS ${ALL_RPC_FUNCS})