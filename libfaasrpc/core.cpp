#include "faasrpc/core.h"

#include <rpc.h>
#include <string>

std::string faasmRpcGetMethod()
{
    int32_t offset = 0;
    int32_t len = 0;
    __faasm_rpc_get_method(&offset, &len);
    return std::string(reinterpret_cast<const char*>(offset), len);
}