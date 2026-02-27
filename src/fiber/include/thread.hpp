#pragma once

#include <pthread.h>
#include <string>
#include <memory>
#include <unistd.h>

#include "monsoon.hpp"

namespace monsoon {

class Thread {
public:
    using ThreadPtr = std::shared_ptr<Thread>;

    Thread(funcCallBack cb, const std::string& name);
    ~Thread();

    pid_t GetTid() const { return tid_; }
    const std::string& getName() const { return name_; }

    void join();
    static Thread* GetThis();
    static const std::string& GetName();
    static void SetName(const std::string& name);
private:
    // 禁止拷贝和移动构造函数以及赋值运算符，线程对象不允许被复制或移动
    Thread(const Thread&) = delete;
    Thread(const Thread&&) = delete;
    Thread& operator=(const Thread&) = delete;
    Thread& operator=(const Thread&&) = delete;

    static void* run(void* args);

private:
    pid_t tid_;
    pthread_t thread_;
    funcCallBack cb_;
    std::string name_;
};

}