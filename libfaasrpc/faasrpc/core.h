#pragma once

#include <string>

// Returns method string allocated in WASM heap — caller owns it
// (or just use it before any allocation that might trigger GC)
std::string faasmRpcGetMethod();