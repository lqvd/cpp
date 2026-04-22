cmake_minimum_required(VERSION 3.8)
project(faasrpc)

# ------------------------------------------
# This builds the Faabric RPC library to wasm and
# adds the relevant .imports file
# ------------------------------------------

set(CMAKE_CXX_STANDARD 17)

set(PUBLIC_HEADERS
    faasrpc/faasrpc.h
)

set(LIB_FILES
    faasrpc.cpp
)

if(BUILD_SHARED_LIBS)
    add_library(faasrpc SHARED ${LIB_FILES})
else()
    add_library(faasrpc STATIC ${LIB_FILES})
endif()

set_target_properties(faasrpc PROPERTIES PUBLIC_HEADER "${PUBLIC_HEADERS}")

# Install Faabric RPC library
set(FAABRIC_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../third-party/faabric/src/rpc)
set(FAABRIC_BINARY_DIR ${FAABRIC_SOURCE_DIR}/build)

add_subdirectory(${FAABRIC_SOURCE_DIR} ${FAABRIC_BINARY_DIR})
add_dependencies(faasrpc faabricrpc)

if (CMAKE_SYSTEM_NAME STREQUAL "WASI")
    message(STATUS "Libfaasrpc WebAssembly build")

    install(
        FILES ${FAABRIC_BINARY_DIR}/libfaabricrpc.a
        DESTINATION $ENV{FAASM_WASM_LIB_INSTALL_DIR}
    )

    install(
        TARGETS faasrpc
        ARCHIVE DESTINATION $ENV{FAASM_WASM_LIB_INSTALL_DIR}
        LIBRARY DESTINATION $ENV{FAASM_WASM_LIB_INSTALL_DIR}
        PUBLIC_HEADER DESTINATION $ENV{FAASM_WASM_HEADER_INSTALL_DIR}/faasmrpc
    )

    # Add imports file
    install(
        FILES libfaasrpc.imports
        DESTINATION $ENV{FAASM_WASM_LIB_INSTALL_DIR}
        RENAME libfaasrpc.imports
    )

    # Symlink library as librpc
    install(
        CODE "execute_process( \
        COMMAND
            ${CMAKE_COMMAND} -E create_symlink \
            $ENV{FAASM_WASM_LIB_INSTALL_DIR}/libfaabricrpc.a \
            $ENV{FAASM_WASM_LIB_INSTALL_DIR}/librpc.a \
        )"
    )

    # Symlink rpc.h
    install(
        CODE "execute_process( \
        COMMAND ${CMAKE_COMMAND} -E create_symlink \
            $ENV{FAASM_WASM_LIB_INSTALL_DIR}/include/faabric/rpc/rpc.h \
            $ENV{FAASM_WASM_HEADER_INSTALL_DIR}/rpc.h   \
        )"
    )
else()
    message(STATUS "Libfaasrpc native build")

    install(
        TARGETS faasrpc
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        PUBLIC_HEADER DESTINATION include/faasrpc
    )

    # Symlink rpc.h
    install(
        CODE "execute_process( \
        COMMAND ${CMAKE_COMMAND} -E create_symlink \
            ${CMAKE_INSTALL_PREFIX}/include/faabric/rpc/rpc.h \
            ${CMAKE_INSTALL_PREFIX}/include/rpc.h   \
        )"
    )
endif()
