set(FAASM_USER snb)

# --------------
# Source layout
# --------------
set(SNB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(SNB_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

file(MAKE_DIRECTORY "${SNB_GENERATED_DIR}")

set(PLUGIN_BIN "${CMAKE_SOURCE_DIR}/../build/protogen-faabric/protoc-gen-faabric")

if (NOT EXISTS "${PLUGIN_BIN}")
    message(FATAL_ERROR
        "Could not find protoc-gen-faabric at ${PLUGIN_BIN}. "
        "Run inv pb.build-plugin first."
    )
endif()

# --------------
# Protobuf / Faabric stub generation
# --------------
set(SNB_PROTOS
    "${SNB_ROOT}/benchmark.proto" 
    "${SNB_ROOT}/common/social_types.proto"

    "${SNB_ROOT}/composePost/proto/compose_post.proto"
    "${SNB_ROOT}/postStorage/proto/post_storage.proto"
    "${SNB_ROOT}/text/proto/text.proto"
    "${SNB_ROOT}/unique_id/proto/unique_id.proto"
    "${SNB_ROOT}/user/proto/user.proto"
    "${SNB_ROOT}/user_db/proto/user_db.proto"
)

set(SNB_GENERATED_PB_SRCS "")
set(SNB_ALL_GENERATED "")

foreach(PROTO_FILE IN LISTS SNB_PROTOS)
    if (NOT EXISTS "${PROTO_FILE}")
        message(FATAL_ERROR "Missing proto file: ${PROTO_FILE}")
    endif()

    file(RELATIVE_PATH PROTO_REL "${SNB_ROOT}" "${PROTO_FILE}")
    get_filename_component(PROTO_REL_DIR "${PROTO_REL}" DIRECTORY)
    get_filename_component(PROTO_FN "${PROTO_FILE}" NAME_WE)

    set(GEN_DIR "${SNB_GENERATED_DIR}/${PROTO_REL_DIR}")

    set(GEN_PB_CC "${GEN_DIR}/${PROTO_FN}.pb.cc")
    set(GEN_PB_H "${GEN_DIR}/${PROTO_FN}.pb.h")
    set(GEN_FAABRIC_H "${GEN_DIR}/${PROTO_FN}.faabric.h")

    add_custom_command(
        OUTPUT
            "${GEN_PB_CC}"
            "${GEN_PB_H}"
            "${GEN_FAABRIC_H}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${GEN_DIR}"
        COMMAND protoc
        ARGS
            "--plugin=protoc-gen-faabric=${PLUGIN_BIN}"
            "--faabric_out=${SNB_GENERATED_DIR}"
            "--cpp_out=${SNB_GENERATED_DIR}"
            -I "${SNB_ROOT}"
            "${PROTO_FILE}"
        DEPENDS
            "${PROTO_FILE}"
            ${SNB_PROTOS}
            "${PLUGIN_BIN}"
        COMMENT "Generating SNB stubs for ${PROTO_REL}"
        VERBATIM
    )

    list(APPEND SNB_GENERATED_PB_SRCS "${GEN_PB_CC}")

    list(APPEND SNB_ALL_GENERATED
        "${GEN_PB_CC}"
        "${GEN_PB_H}"
        "${GEN_FAABRIC_H}"
    )
endforeach()

add_custom_target(snb_generate_proto_stubs
    DEPENDS ${SNB_ALL_GENERATED}
)

# --------------
# Generated protobuf message library
# --------------
add_library(snb_generated_pb STATIC
    ${SNB_GENERATED_PB_SRCS}
)

add_dependencies(snb_generated_pb
    snb_generate_proto_stubs
)

target_include_directories(snb_generated_pb PUBLIC
    "${SNB_GENERATED_DIR}"
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

    target_include_directories(snb_generated_pb PUBLIC
        "${FAASM_WASM_SYSROOT}/include"
    )

    target_link_libraries(snb_generated_pb PUBLIC
        "${WASM_PROTOBUF_LITE}"
    )
else()
    find_package(Protobuf REQUIRED)

    target_include_directories(snb_generated_pb PUBLIC
        ${Protobuf_INCLUDE_DIRS}
    )

    target_link_libraries(snb_generated_pb PUBLIC
        ${Protobuf_LIBRARIES}
    )
endif()

# --------------
# Header-only generated Faabric RPC stubs
# --------------
add_library(snb_generated_stubs INTERFACE)

add_dependencies(snb_generated_stubs
    snb_generate_proto_stubs
)

target_include_directories(snb_generated_stubs INTERFACE
    "${SNB_GENERATED_DIR}"
)

# --------------
# SNB functions
# --------------
set(ALL_SNB_FUNCS "")

function(snb_configure_func exec_name)
    target_link_libraries(${exec_name}
        faasrpc
        rpc
        snb_generated_stubs
        snb_generated_pb
    )

    target_include_directories(${exec_name} PRIVATE
        "${SNB_ROOT}"
        "${SNB_GENERATED_DIR}"
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
endfunction()

function(snb_func exec_name service_dir)
    set(SERVER_SRC "${SNB_ROOT}/${service_dir}/faasm/Server.cpp")

    if (NOT EXISTS "${SERVER_SRC}")
        message(FATAL_ERROR
            "Missing Faasm server for ${service_dir}: ${SERVER_SRC}"
        )
    endif()

    faasm_func(${exec_name} "${SERVER_SRC}")
    snb_configure_func(${exec_name})

    set(ALL_SNB_FUNCS ${ALL_SNB_FUNCS} ${exec_name} PARENT_SCOPE)
endfunction()

function(snb_func_src exec_name source_file)
    set(SRC "${SNB_ROOT}/${source_file}")

    if (NOT EXISTS "${SRC}")
        message(FATAL_ERROR
            "Missing SNB function source for ${exec_name}: ${SRC}"
        )
    endif()

    faasm_func(${exec_name} "${SRC}")
    snb_configure_func(${exec_name})

    set(ALL_SNB_FUNCS ${ALL_SNB_FUNCS} ${exec_name} PARENT_SCOPE)
endfunction()

snb_func(ComposePostService composePost)
snb_func(UserDbService user_db)
snb_func(PostStorageService postStorage)
snb_func(TextService text)
snb_func(UniqueIdService unique_id)
snb_func(UserService user)

snb_func_src(benchmark_snb benchmark.cpp)

message(STATUS "SNB funcs: ${ALL_SNB_FUNCS}")

add_custom_target(snb_all_funcs
    DEPENDS ${ALL_SNB_FUNCS}
)