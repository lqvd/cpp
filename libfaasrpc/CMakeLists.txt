cmake_minimum_required(VERSION 3.8)
project(libfaasrpc)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PUBLIC_HEADERS
    faasrpc/faasrpc.h
    faasrpc/RpcCall.h
    faasrpc/Task.h
)

set(LIB_FILES
    coro_trampoline.cpp
    faasrpc.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    message(STATUS "Libfaasrpc WebAssembly build")

    add_library(faasrpc STATIC ${LIB_FILES})
    set_target_properties(faasrpc PROPERTIES PUBLIC_HEADER "${PUBLIC_HEADERS}")

    # Install faabricrpc stub library
    set(FAABRIC_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../third-party/faabric/src/rpc)
    set(FAABRIC_BINARY_DIR ${FAABRIC_SOURCE_DIR}/build)
    add_subdirectory(${FAABRIC_SOURCE_DIR} ${FAABRIC_BINARY_DIR})
    add_dependencies(faasrpc faabricrpc)

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

    install(
        FILES libfaasrpc.imports
        DESTINATION $ENV{FAASM_WASM_LIB_INSTALL_DIR}
    )

    install(CODE
        "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink \
            $ENV{FAASM_WASM_LIB_INSTALL_DIR}/libfaabricrpc.a \
            $ENV{FAASM_WASM_LIB_INSTALL_DIR}/librpc.a)"
    )

    install(CODE
        "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink \
            $ENV{FAASM_WASM_HEADER_INSTALL_DIR}/faasrpc/rpc.h \
            $ENV{FAASM_WASM_HEADER_INSTALL_DIR}/rpc.h)"
    )

else()
    message(STATUS "Libfaasrpc native build")

    if(BUILD_SHARED_LIBS)
        add_library(faasrpc SHARED ${LIB_FILES})
    else()
        add_library(faasrpc STATIC ${LIB_FILES})
    endif()

    set_target_properties(faasrpc PROPERTIES PUBLIC_HEADER "${PUBLIC_HEADERS}")

    install(
        TARGETS faasrpc
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        PUBLIC_HEADER DESTINATION include/faasrpc
    )
endif()

target_include_directories(faasrpc PUBLIC ${CMAKE_CURRENT_LIST_DIR})