add_library(rpc_dist_test_common Ping.cpp)

target_include_directories(rpc_dist_test_common PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(rpc_dist_test_common PUBLIC faasrpc rpc)

target_compile_features(rpc_dist_test_common PUBLIC cxx_std_20)

if (NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
    target_link_libraries(rpc_dist_test_common PUBLIC pthread)
endif ()