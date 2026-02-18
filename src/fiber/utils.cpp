#include <iostream>
#include <unistd.h>
#include <sys/syscall.h>
#include <cxxabi.h>
#include <vector>
#include <execinfo.h>
#include <sstream>
#include <cassert>

#include "utils.hpp"

namespace monsoon {
    pid_t GetThreadId() {
        return syscall(SYS_gettid);
    }

    u_int32_t GetFiberId() {
        // 暂时还没有是实现
        return 0;
    }
    

    // 获取当前启动的毫秒数
    // 系统从启动到当前时刻的毫秒数
    uint64_t GetElapsedMS() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);    // CLOCK_MONOTONIC_RAW：纯硬件时间，不受NTP影响
        return static_cast<uint64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
    }

    // 将原始函数名解析为可读函数名：让报错日志更可读
    static std::string Demangle(const char* str) {
        size_t size = 0;
        int status = 0;
        std::string rt;
        rt.resize(256);

        // 提取函数名,sscanf 返回成功赋值的字段数。应该为1个字段
        if(sscanf(str, "%*[^(]%*[^_]%255[^)+]", rt.data()) == 1) {
            // 输出缓冲区为 nullptr。返回随机分配的一块接收内存
            char* demangled = abi::__cxa_demangle(rt.data(), nullptr, &size, &status);
            if(demangled) {
                std::string result(demangled);
                free(demangled);
                return result;
            }
        }

        // 解析失败，返回原始函数名
        if(sscanf(str, "%255s", rt.data()) == 1) {
            return rt;
        }

        return str; // 返回原始字符串
    }

    // 获取当前线程的调用栈信息
    static void Backtrace(std::vector<std::string>& bt, int size, int skip) {
        // 分配用于存储调用栈信息的数组
        void** array = (void **)malloc((sizeof(void *) * size));
        size_t sz = ::backtrace(array, size);   //  获取程序中当前函数的回溯信息,结构存储在array中

        // 将每一个backtrace的返回值都翻译成“函数名+函数内偏移量+函数返回值”
        char** strings = ::backtrace_symbols(array, sz);
        if(strings == NULL) {
            std::cout << "backtrace_symbols error" << std::endl;
            return;
        }

        // 解析每一个调用栈的信息，并将解析后的函数名添加到bt中
        for(int i=0; i<sz; i++){
            bt.push_back(Demangle(strings[i]));
        }

        free(strings);
        free(array);
    }

    // skip：通常我们在打印堆栈时，不想看到Backtrace函数本身 和 调用它的日志函数，所以需要跳过栈顶的几层。
    static std::string BacktraceToString(int size, int skip, const std::string& prefix) {
        std::vector<std::string> bt;
        Backtrace(bt, size, skip);
        std::stringstream ss;
        for(const auto& str : bt){
            ss << prefix << str << std::endl;
        }

        return ss.str();
    }

    // 当 CondPanic 触发时，它能告诉我们完整的调用链。用于处理分布式环境中复杂的状态错误
    void CondPanic(bool condition, std::string err_str){
        if(!condition){
            std::cout << "[assert by] (" << __FILE__ << ":" << __LINE__ << "),err: " << err_str << std::endl;
            std::cout << "[backtrace]\n" << BacktraceToString(6, 3, "") << std::endl;
            assert(condition);
        }
    }
}