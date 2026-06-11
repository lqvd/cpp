
#include <faasrpc/Driver.h>

#include <coroutine>

namespace faabric::rpc {

namespace {

std::coroutine_handle<> serverRoot = nullptr;

}

void setServerRoot(std::coroutine_handle<> h) { serverRoot = h; }

void runServerDriver()
{
    while (serverRoot && !serverRoot.done()) {
        serverRoot.resume();
    }
}

}