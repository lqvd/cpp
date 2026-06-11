#include <coroutine>

namespace faabric::rpc {

void setServerRoot(std::coroutine_handle<> h);

void runServerDriver();

}