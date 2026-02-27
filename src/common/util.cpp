#include <ctime>
#include <cstdarg>
#include <cstdio>
#include <chrono>

#include "util.hpp"
#include "config.hpp"

// 增强版的printf函数，打印当前时间和参数信息
void DPrintf(const char* format, ...){
    if(Debug){
        time_t now = time(nullptr);
        tm *now_tm = localtime(&now);

        va_list args;
        va_start(args, format);
        std::printf("[%d-%d-%d-%d-%d-%d]", now_tm->tm_year + 1900, now_tm->tm_mon + 1,
                    now_tm->tm_mday, now_tm->tm_hour, now_tm->tm_min, now_tm->tm_sec);
        std::vprintf(format, args); // 使用可变参数列表进行格式化输出
        std::printf("\n");
        va_end(args);
    }
}

// assert函数，打印错误信息
void myAssert(bool condition, std::string message){
    if(!condition){
        std::cerr << "Error: " << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

// 获取当前时间点
std::chrono::_V2::system_clock::time_point now(){
    return std::chrono::high_resolution_clock::now();
}

// 获取一个随机的选举超时时间，范围在[minRandomizedElectionTimeout, maxRandomizedElectionTimeout]之间
std::chrono::milliseconds getRandomizedElectionTimeout(){
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> dist(minRandomizedElectionTimeout, maxRandomizedElectionTimeout);
    return std::chrono::milliseconds(dist(rng));
}

// 睡眠n毫秒
void sleepNMilliseconds(int n){
    std::this_thread::sleep_for(std::chrono::milliseconds(n));
}