#pragma once

#include <pthread.h>
#include <cstdint>
#include <sys/types.h>
#include <string>

namespace monsoon {
pid_t GetThreadId();

u_int32_t GetFiberId();

uint64_t GetElapsedMS();

void CondPanic(bool condition, std::string err_str);

}