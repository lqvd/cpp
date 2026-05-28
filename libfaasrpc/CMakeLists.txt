cmake_minimum_required(VERSION 3.8)
project(libfaasrpc)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PUBLIC_HEADERS
    faasrpc/Channel.h
    faasrpc/ClientContext.h
    faasrpc/coro_trampoline.h
    faasrpc/faasrpc.h
    faasrpc/MigrationCheckpoint.h
    faasrpc/RpcCall.h
    faasrpc/RpcReceive.h
    faasrpc/Server.h
    faasrpc/ServerBuilder.h
    faasrpc/Service.h
    faasrpc/Status.h
    faasrpc/Task.h
)

set(LIB_FILES
    Channel.cpp
    coro_trampoline.cpp
    faasrpc.cpp
    Server.cpp
    ServerBuilder.cpp
)

set(FAABRIC_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../third-party/faabric)
set(FAABRIC_SOURCE_DIR ${FAABRIC_ROOT_DIR}/src/rpc)
set(FAABRIC_INCLUDE_DIR ${FAABRIC_ROOT_DIR}/include)

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    message(STATUS "Libfaasrpc WebAssembly build")

    add_library(faasrpc STATIC ${LIB_FILES})
    set_target_properties(faasrpc PROPERTIES PUBLIC_HEADER "${PUBLIC_HEADERS}")

    # Build faabric rpc stub library from source
    set(FAABRIC_BINARY_DIR ${FAABRIC_SOURCE_DIR}/build)
    add_subdirectory(${FAABRIC_SOURCE_DIR} ${FAABRIC_BINARY_DIR})

    target_include_directories(faasrpc PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${FAABRIC_SOURCE_DIR}
        ${FAABRIC_INCLUDE_DIR}
    )

    target_link_libraries(faasrpc PUBLIC faabricrpc)

    install(
        FILES ${FAABRIC_BINARY_DIR}/libfaabricrpc.a
        DESTINATION $ENV{FAASM_WASM_LIB_INSTALL_DIR}
    )

    install(
        TARGETS faasrpc
        ARCHIVE DESTINATION $ENV{FAASM_WASM_LIB_INSTALL_DIR}
        LIBRARY DESTINATION $ENV{FAASM_WASM_LIB_INSTALL_DIR}
        PUBLIC_HEADER DESTINATION $ENV{FAASM_WASM_HEADER_INSTALL_DIR}/faasrpc
    )

    # Install canonical faabric RPC C header path
    install(
        FILES ${FAABRIC_INCLUDE_DIR}/faabric/rpc/rpc.h
        DESTINATION $ENV{FAASM_WASM_HEADER_INSTALL_DIR}/faabric/rpc
    )

    # Optional flat include path compatibility
    install(CODE
        "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink \
            $ENV{FAASM_WASM_HEADER_INSTALL_DIR}/faabric/rpc/rpc.h \
            $ENV{FAASM_WASM_HEADER_INSTALL_DIR}/rpc.h)"
    )

    # Optional librpc compatibility link
    install(CODE
        "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink \
            $ENV{FAASM_WASM_LIB_INSTALL_DIR}/libfaabricrpc.a \
            $ENV{FAASM_WASM_LIB_INSTALL_DIR}/librpc.a)"
    )

    install(
        FILES libfaasrpc.imports
        DESTINATION $ENV{FAASM_WASM_LIB_INSTALL_DIR}
    )

else()
    message(STATUS "Libfaasrpc native build")

    if(BUILD_SHARED_LIBS)
        add_library(faasrpc SHARED ${LIB_FILES})
    else()
        add_library(faasrpc STATIC ${LIB_FILES})
    endif()

    set_target_properties(faasrpc PROPERTIES PUBLIC_HEADER "${PUBLIC_HEADERS}")

    set(FAABRIC_BINARY_DIR ${FAABRIC_SOURCE_DIR}/build-native)
    add_subdirectory(${FAABRIC_SOURCE_DIR} ${FAABRIC_BINARY_DIR})

    target_include_directories(faasrpc PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${FAABRIC_SOURCE_DIR}
        ${FAABRIC_INCLUDE_DIR}
    )

    target_link_libraries(faasrpc PUBLIC faabricrpc)

    install(
        TARGETS faasrpc
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        PUBLIC_HEADER DESTINATION include/faasrpc
    )

    install(
        FILES ${FAABRIC_INCLUDE_DIR}/faabric/rpc/rpc.h
        DESTINATION include/faabric/rpc
    )
endif()

target_compile_features(faasrpc PUBLIC cxx_std_20)