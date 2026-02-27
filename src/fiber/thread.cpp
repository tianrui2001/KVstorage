#include "thread.hpp"
#include "utils.hpp"

namespace monsoon {

static thread_local Thread* cur_thread = nullptr; // 当前线程的指针
static thread_local std::string cur_thread_name = "UNKNOW"; // 当前线程的名字

Thread::Thread(funcCallBack cb, const std::string& name = "UNKNOW")
    : cb_(cb), name_(name) 
{
    if(name.empty()) {
        name_ = "UNKNOW";
    }

    int rt = pthread_create(&thread_, nullptr, &Thread::run, this);
    if(rt) {
        throw std::runtime_error("pthread_create error");
    }
}

Thread::~Thread(){
    if(thread_) {
        pthread_detach(thread_);    // 线程分离，线程结束后会自动回收资源
    }
}

void* Thread::run(void* args) {
    Thread* thread = static_cast<Thread*>(args);
    cur_thread = thread;    // 设置当前线程的指针
    cur_thread_name = thread->name_;    // 设置当前线程的名字
    thread->tid_ = monsoon::GetThreadId();    // 获取当前线程的tid

    // 给线程命名，Linux 内核规定线程名称最长只能是 16 个字符
    pthread_setname_np(pthread_self(), thread->name_.substr(0, 15).c_str());

    funcCallBack cb;
    cb.swap(thread->cb_);   // 交换函数对象，避免线程函数执行过程中线程对象被销毁
    cb();

    return nullptr;
}

void Thread::join() {
    if(thread_){
        int rt = pthread_join(thread_, nullptr);
        if(rt){
            throw std::runtime_error("pthread_join error");
        }

        thread_ = 0;
    }
}

Thread* Thread::GetThis() { return cur_thread; }

const std::string& Thread::GetName() { return cur_thread_name; }

void Thread::SetName(const std::string& name){
    if(name.empty()){
        return;
    }

    // 当前线程存在，设置当前线程对象里面的名字
    if(cur_thread) {
        cur_thread->name_ = name;
    }
    // 如果当前线程不存在，则直接设置当前线程的名字
    cur_thread_name = name;
}
}