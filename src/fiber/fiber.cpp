#include <thread>
#include <atomic>

#include "fiber.hpp"
#include "utils.hpp"
#include "scheduler.hpp"

namespace monsoon
{

const bool DEBUG = true;

static thread_local Fiber* cur_fiber = nullptr; // 当前协程
static thread_local Fiber::FiberPtr cur_thread_main_fiber = nullptr; // 当前线程的主协程（Main Fiber）
static std::atomic<uint64_t> cur_fiber_id{0}; // 协程ID生成器，原子变量保证线程安全
static std::atomic<uint64_t> fiber_count{0}; // 当前线程的协程数量，原子变量保证线程安全
const int g_fiber_stack_size = 128 * 1024; // 默认协程栈大小，128KB

class StackAllocator
{
public:
    static void* Alloc(size_t size) { return malloc(size); }
    static void Delete(void* vp, size_t size) { free(vp); }
};

// 有参构造，并为新的子协程创建栈空间
Fiber::Fiber(funcCallBack cb, size_t stack_size, bool isRunInScheduler)
    : id_(cur_fiber_id++), cb_(cb), isRunInScheduler_(isRunInScheduler)
{
    fiber_count++;
    stack_size_ = stack_size ? stack_size : g_fiber_stack_size;
    stack_ptr = StackAllocator::Alloc(stack_size_); // 为协程分配
    CondPanic(getcontext(&ctx_) == 0, "getcontext error"); // 获取当前上下文，保存到 ctx_ 中

    // 初始化上下文
    ctx_.uc_link = nullptr;          // 协程结束后不需要跳回到哪里
    ctx_.uc_stack.ss_sp = stack_ptr; // 协程栈指针
    ctx_.uc_stack.ss_size = stack_size_; // 协程栈大小
    makecontext(&ctx_, &Fiber::mainFunc, 0); // 修改指令指针，指向 MainFunc（协程的入口函数），但是不会立刻进入
}

Fiber::Fiber(){
    id_ = cur_fiber_id++;
    fiber_count++;
    state_ = RUNNING;
    setCurFiber(this); // 设置当前协程为 this
    CondPanic(getcontext(&ctx_) == 0, "getcontext error"); // 获取当前上下文，保存到 ctx_ 中
}

Fiber::~Fiber(){
    fiber_count--;
    if(stack_ptr) {
        // 有栈空间，说明是子协程
        CondPanic(state_ == TERM, "Fiber is not terminated"); // 子协程必须处于终止态才能销毁
        StackAllocator::Delete(stack_ptr, stack_size_);
    } else {
        CondPanic(!cb_, "Main Fiber's cb_ is not empty"); // 主协程的回调函数必须为空
        CondPanic(state_ == RUNNING, "Main Fiber is not running"); // 主协程必须处于运行态才能销毁

        Fiber* cur = cur_fiber;
        if(cur == this){
            setCurFiber(nullptr);
        }
    }
}

// 协程重置（复用已经结束的协程，复用其栈空间，创建新协程）
void Fiber::reset(funcCallBack cb){
    CondPanic(stack_ptr, "Cannot reset a Fiber without stack space"); // 必须有栈空间才能重置
    CondPanic(state_ == TERM, "Fiber is not terminated"); // 必须处于终止态才能重置
    cb_ = cb;
    CondPanic(getcontext(&ctx_) == 0, "getcontext error"); // 获取当前上下文，保存到 ctx_ 中
    ctx_.uc_link = nullptr;
    ctx_.uc_stack.ss_sp = stack_ptr;
    ctx_.uc_stack.ss_size = stack_size_;
    makecontext(&ctx_, &Fiber::mainFunc, 0);
    state_ = READY;
}

// 从 主/调度 协程 -> 切入 -> 子协程
void Fiber::resume(){
    CondPanic(state_ != TERM && state_ != RUNNING, "Fiber is not ready to run"); // 协程必须处于就绪态才能运行
    setCurFiber(this);
    state_ = RUNNING;

    if(isRunInScheduler_) {
        CondPanic(swapcontext(&Scheduler::getMainFiber()->ctx_, &ctx_) == 0, 
                "isRunInScheduler_ = true, swapcontext error");
    } else {
        CondPanic(swapcontext(&cur_thread_main_fiber->ctx_, &ctx_) == 0, 
                "isRunInScheduler_ = false, swapcontext error");
    }
}

// 当前协程让出执行权。协程执行完成之后会自动yield,回到主协程，此时状态为TERM
void Fiber::yield(){
    CondPanic(state_ == RUNNING || state_ == TERM, "Fiber is not running"); // 这里为什么会有 TERM？因为协程执行完毕后会自动 yield
    setCurFiber(cur_thread_main_fiber.get());   // 标记当前运行的变回了主协程
    state_ = READY;

    if(isRunInScheduler_) {
        CondPanic(swapcontext(&Scheduler::getMainFiber()->ctx_, &ctx_) == 0,
                "isRunInScheduler_ = true, swapcontext error");
    } else {
        CondPanic(swapcontext(&cur_thread_main_fiber->ctx_, &ctx_) == 0,
                "isRunInScheduler_ = false, swapcontext error");
    }
}

void Fiber::setCurFiber(Fiber* fiber){ cur_fiber = fiber; }

Fiber::FiberPtr Fiber::getThis(){
    if(cur_fiber) {
        return cur_fiber->shared_from_this();
    }

    // 第一次调用时，当前线程还没有协程的概念
    // 于是创建一个“主协程”对象，把当前线程的上下文这一刻的状态“快照”下来
    FiberPtr main_fiber(new Fiber());
    cur_thread_main_fiber = main_fiber;
    return cur_fiber->shared_from_this();
}

void Fiber::mainFunc(){
    FiberPtr cur = getThis(); // 获取当前协程
    CondPanic(cur != nullptr, "Main Fiber is null");
    cur->cb_();
    cur->cb_ = nullptr;
    cur->state_ = TERM;

    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->yield(); // 执行完毕后自动 yield，回到主协程
}
   
}