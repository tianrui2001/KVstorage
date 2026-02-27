#pragma once

#include <memory>
#include <ucontext.h>

#include "monsoon.hpp"

namespace monsoon
{

// 这份代码深受 Sylar 框架风格的影响，实现了一个非对称协程模型。
// 子协程只能和主协程进行切换。子协程 yield 时，必须回到主协程，不能直接跳到另一个子协程
class Fiber : public std::enable_shared_from_this<Fiber>
{
public:
    using FiberPtr = std::shared_ptr<Fiber>;
    enum State {
        READY,    // 就绪态，刚创建后或者yield后状态
        RUNNING, // 运行态，正在执行中
        TERM,    // 终止态，执行完毕或者被取消
    };

    Fiber(funcCallBack cb, size_t stack_size = 0, bool isRunInScheduler = true);
    ~Fiber();

    void reset(funcCallBack cb);  // 重置协程函数，复用栈空间
    void resume();  // 切换到当前协程执行
    void yield();   // 让出协程的执行权

    uint64_t getId() const { return id_; }
    State getState() const { return state_; }

    static void setCurFiber(Fiber* fiber); // 设置当前协程
    static FiberPtr getThis(); // 获取当前协程
    static void mainFunc(); // 协程回调函数


private:
    // 初始化当前线程的协程功能，构造线程主协程对象（Main Fiber）
    // 它是这个线程上的“皇帝”，其他子协程运行完或者 yield 后，通常会回到它这里。
    Fiber();

    uint64_t id_ = 0;           // 协程ID
    State state_ = READY;       // 协程状态

    size_t stack_size_ = 0;     // 协程栈大小，默认 128KB 或 1MB
    void* stack_ptr = nullptr;    // 协程栈指针
    ucontext_t ctx_;          // 协程上下文, 用来保存 CPU 的寄存器信息

    funcCallBack cb_;       // 协程执行函数
    bool isRunInScheduler_; // 本协程是否参与调度器调度
};

    
} // namespace monsoon