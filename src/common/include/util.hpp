#pragma once

#include <iostream>
#include <string>
#include <stdexcept>
#include <cstdio>
#include <vector>
#include <random>
#include <thread>
#include <queue>
#include <mutex>
#include <sstream>
#include <condition_variable>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>// for serialization

#include "config.hpp"

// 实现一个go语言的defer功能，使用RAII实现
template <typename F>
class DeferClass {
public:
    DeferClass(F&& func) : func_(std::forward<F>(func)) {}
    DeferClass(const F& func) : func_(func) {}
    ~DeferClass() { func_(); }

    DeferClass(const DeferClass&) = delete;     // 禁止拷贝构造
    DeferClass& operator=(const DeferClass&) = delete; // 禁止拷贝赋值

private:
    F func_;
};


#define _CONCAT(a, b) a##b  // 将a和b连接在一起

// 定义一个DeferClass对象，名字为der_placeholder_line，使用lambda表达式作为参数
#define _MAKE_DEFER_(line) DeferClass _CONCAT(der_placeholder_, line) = [&]()

#undef DEFER
#define DEFER _MAKE_DEFER_(__LINE__)

// -----------打印调试相关：------------

void DPrintf(const char* format, ...);
void myAssert(bool condition, std::string message = "Assertion failed!");

template <typename... Args>
std::string format(const char* format_str, Args... args) {
    // C++17 编译期判断：如果没有额外参数，直接返回原始字符串
    if constexpr (sizeof...(args) == 0) {
        return std::string(format_str);
    } else {
        int size = std::snprintf(nullptr, 0, format_str, args...) + 1; // 计算格式化后的字符串长度 + '\0'
        if (size <= 0){
            throw std::runtime_error("Error during formatting.");
        }
        
        std::vector<char> buffer(size);
        std::snprintf(buffer.data(), size, format_str, args...); // 格式化字符串
        return std::string(buffer.data(), buffer.data() + size - 1); // remove '\0'
    }
}

// -----------时间相关：------------

std::chrono::_V2::system_clock::time_point now();
std::chrono::milliseconds getRandomizedElectionTimeout();
void sleepNMilliseconds(int n);

// 异步写日志队列，使用线程安全的队列实现
template <typename T>
class LockQueue {
public:
    void push(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        que_.push(value);
        cond_.notify_one();
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        while(que_.empty()){    // 队列为空时等待, 避免长时间占用锁
            cond_.wait(lock);
        }

        T value = que_.front();
        que_.pop();
        return value;
    }

    // 带超时的pop函数，等待timeout_ms毫秒，
    // 如果超时则返回false，否则返回true并将结果保存在resval中
    bool timeOutPop(int timeout_ms, T* resval) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 获取当前时间
        auto now = std::chrono::system_clock::now();
        auto timeout_time = now + std::chrono::milliseconds(timeout_ms);

        while(que_.empty()){
            if(cond_.wait_until(lock, timeout_time) == std::cv_status::timeout){
                return false;
            } else {
                continue;
            }
        }

        *resval = que_.front();
        que_.pop();
        return true;
    }

private:
    std::queue<T> que_;
    std::mutex mutex_;
    std::condition_variable cond_;
};


// 这个Op是kv传递给raft的command
class Op {
public:
    std::string Operation; // 操作类型
    std::string Key;       // 键
    std::string Value;     // 值
    std::string ClientId; // 客户端ID
    int RequestId; // 请求ID

public:

    // 使用Boost序列化库将对象序列化为字符串
    std::string asString() const {
        std::stringstream ss;
        boost::archive::text_oarchive oa(ss);
        oa  << *this; // 序列化当前对象
        return ss.str(); // 返回序列化后的字符串
    }

    // 使用Boost序列化库从字符串反序列化对象
    // 如果反序列化失败，返回false; 否则则为true
    bool parseFromString(std::string str) {
        std::stringstream ss(str);
        boost::archive::text_iarchive ia(ss);

        try {
            ia >> *this; // 反序列化到当前对象
            return true;
        } catch (const std::exception& e){
            std::cerr << "Error parsing Op from string: " << e.what() << std::endl;
            return false;
        }
    }


    // 重载了 << 操作符，目的是为了让 Op 对象可以被方便地打印到标准输出流
    friend std::ostream& operator<<(std::ostream& os, const Op& op) {
        os << "[MyClass:Operation{" + op.Operation + "},Key{" + op.Key + "},Value{" + op.Value + 
        "},ClientId{" + op.ClientId + "},RequestId{" + std::to_string(op.RequestId) + "}]";
        return os;
    }

private:
    friend class boost::serialization::access;  // 需要Boost序列化库的访问权限

    // 执行 oa << *this 或 ia >> *this 时, Boost库会自动调用这个函数
    // 进行序列化和反序列化
    // 注意：version参数是Boost序列化库的版本控制参数，通常不需要使用
    template<typename Archive>
    void serialize(Archive& ar, const unsigned int version) {
        // & 操作符被 Boost 重载了。输出相当于<<符; 输入相当于>>符
        ar& Operation;
        ar& Key;
        ar& Value;
        ar& ClientId;
        ar& RequestId;
    }
};


// KVServer 回复的错误类型
const std::string OK = "OK";
const std::string ErrNoKey = "ErrNoKey";
const std::string ErrWrongLeader = "ErrWrongLeader";

// 后面还有一些，待实现...