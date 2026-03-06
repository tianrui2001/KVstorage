#include <iostream>

#include "scheduler.hpp"
#include "utils.hpp"
#include "hook.hpp"

namespace monsoon {

// TLS 存储区:
// 当前线程的调度器，同一调度器下的所有线程共享同一调度器实例
static thread_local Scheduler* cur_scheduler = nullptr;
// 当前线程的调度协程，每个线程一个 (协程级调度器)
static thread_local Fiber* cur_thread_scheduler_fiber = nullptr;


const std::string LOG_HEAD = "[Scheduler]: ";

Scheduler::Scheduler(size_t threadCnt, bool useCaller, const std::string& name)
    : name_(name), 
    isUserCaller_(useCaller) 
{
    CondPanic(threadCnt > 0, "threadCnt <= 0");
    threadCnt_ = threadCnt;

    // use_caller:是否将当前线程也作为被调度线程
    if(isUserCaller_){
        std::cout << LOG_HEAD << "current thread as called thread" << std::endl;
        // 总线程数减1，因为当前线程算一个，所以需要创建的新线程数减 1
        --threadCnt_;

        // 初始化caller线程的主协程
        Fiber::getThis();
        std::cout << LOG_HEAD << "init caller thread's main fiber success" << std::endl;
        CondPanic(getThis() == nullptr, "getThis() is null");
        cur_scheduler = this;

        // rootFiber_ 是调度器的主协程，他和线程的主协程 (cur_thread_fiber) 是两个不同的协程
        rootFiber_.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false));
        cur_thread_scheduler_fiber = rootFiber_.get();
        Thread::SetName(name);
        rootThreadId_ = GetThreadId();
        threadIds_.push_back(rootThreadId_);
    } else {
        rootThreadId_ = -1; // 非调用线程模式下，rootThreadId_ 不需要设置
    }

    std::cout << "-------scheduler init success-------" << std::endl;
}

const std::string& Scheduler::getName() const { return name_; }

// 获取当前线程的调度器协程
Fiber* Scheduler::getMainFiber(){ return cur_thread_scheduler_fiber; }

// 获取当前线程调度器
Scheduler* Scheduler::getThis() { return cur_scheduler; }

// 设置当前线程调度器
void Scheduler::setThis() { cur_scheduler = this; }

// 唤醒 idle 协程，这里是虚函数，在派生类实现
void Scheduler::trickle() {
    std::cout << LOG_HEAD << "trickle called" << std::endl;
}

Scheduler::~Scheduler() {
    CondPanic(isStopped_, "Scheduler is not stopped yet");
    if(getThis() == this) {
        cur_scheduler = nullptr; // 清除当前线程的调度器
    }
}

// 调度器的主循环，执行任务
void Scheduler::run() {
    std::cout << LOG_HEAD << "begin run" << std::endl;
    setHookEnable(true); // 启用钩子
    setThis();

    // 3. 初始化当前线程的调度协程
    if(GetThreadId() != rootThreadId_) {
        // 当新线程刚开始执行的时候还没有协程的概念，所以这里返回的是调度协程
        cur_thread_scheduler_fiber = Fiber::getThis().get();
    }

    // 创建idle协程：当任务队列为空时，线程不能死循环空转（占满 CPU），需要切到 idle 协程去休眠
    Fiber::FiberPtr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));     // 待定

    // 5. 准备一个复用的协程对象，用于执行回调函数任务
    Fiber::FiberPtr cb_fiber;
    SchedulerTask task;

    while(true) {
        task.reset(); // 清空任务
        bool trick_me = false; // 是否需要唤醒 idle 协程
        {
            Mutex::Lock lock(mutex_);
            auto it = tasks_.begin();
            while (it != tasks_.end()) {
                // 如果这个任务指定了要在别的线程跑
                // 需要通知其他线程进行调度，并跳过当前任务
                if(it->threadId_ != -1 && it->threadId_ != GetThreadId()){
                    it++;
                    trick_me = true; // 需要唤醒 idle 协程
                    continue;
                }

                CondPanic(it->fiber_ || it->cb_, "task is nullptr");
                if (it->fiber_) {
                    CondPanic(it->fiber_->getState() == Fiber::READY, "fiber task state error");
                }

                task = *it; // 取出任务
                it = tasks_.erase(it); // 从任务队列中删除任务
                activeThreadCnt_++; // 活跃线程数加1
                break;
            }

            // 还有剩余任务吗？如果我拿走一个后队列还不空，说明任务很多，
            // 赶紧 tickle 唤醒其他睡觉的兄弟一起来干活
            trick_me |= (it != tasks_.end());
        }

        if(trick_me) {
            trickle(); // 实则调用派生类的 trickle() 方法
        }

        // 判断任务是 fiber 还是 cb， 还是 idle 协程
        if(task.fiber_) {
            task.fiber_->resume(); // 切换到协程执行

            // 执行结束
            activeThreadCnt_--;
            task.reset();
        } else if(task.cb_) {
            if(cb_fiber) {  // 之前创建过，复用之前的
                cb_fiber->reset(task.cb_);
            } else {    // 第一次创建
                cb_fiber.reset(new Fiber(task.cb_));
            }

            cb_fiber->resume();

            activeThreadCnt_--;
            task.reset();
            cb_fiber.reset();
        } else {
            // 任务队列为空
            // 检查 Idle 协程是不是结束了（意味着 Scheduler::stop 被调用且满足停止条件）
            if(idle_fiber->getState() == Fiber::TERM) {
                break; // 退出调度循环
            }

            // idle 协程不断空转
            idleThreadCnt_++;
            idle_fiber->resume(); // 切换到 idle 协程执行
            idleThreadCnt_--;
        }
    }

    std::cout << "run exit" << std::endl;
}

void Scheduler::idle() {
    while(!stopping()) {
        Fiber::getThis()->yield(); // 让出执行权，回到调度器
    }
}

// 调度器启动, 初始化调度线程池
void Scheduler::start(){
    std::cout << LOG_HEAD << "scheduler start" << std::endl;

    Mutex::Lock lock(mutex_);
    if(isStopped_ || !threadPool_.empty()) {
        if(isStopped_) {
            std::cout << LOG_HEAD << "scheduler already stopped" << std::endl;
        } else {
            std::cout << LOG_HEAD << "thread pool is not empty" << std::endl;
        }
        return; // 如果已经停止了，就不再启动
    }

    threadPool_.resize(threadCnt_);
    for(size_t i = 0; i < threadCnt_; i++){
        threadPool_[i].reset(new Thread(std::bind(&Scheduler::run, this), 
                                name_ + "_" + std::to_string(i)));
        threadIds_.push_back(threadPool_[i]->GetTid());
    }
}

bool Scheduler::stopping() {
    Mutex::Lock lock(mutex_);
    return isStopped_ && activeThreadCnt_ == 0 && tasks_.empty();
}

// 使用caller线程，则调度线程依赖stop()来执行caller线程的调度协程
// 不使用caller线程，只用caller线程去调度，则调度器真正开始执行的位置是stop()
void Scheduler::stop(){
    std::cout << LOG_HEAD << "stop" << std::endl;
    if(stopping()) {
        return; // 如果已经停止了，就不再执行
    }

    isStopped_ = true;
    // 检查：只有 Caller 线程（发起 start 的那个线程）才有资格调用 stop
    if (isUserCaller_) {
        CondPanic(getThis() == this, "cur thread is not caller thread");
    } else {
        CondPanic(getThis() != this, "cur thread is caller thread");
    }

    for(size_t i = 0; i < threadCnt_; i++){
        trickle(); // 唤醒所有 idle 协程
    }

    // 只有在 use_caller 为 true 时，rootFiber_ 才会被创建
    if(rootFiber_) {
        trickle(); // 唤醒 rootFiber_ 协程
        rootFiber_->resume(); // 切换到 rootFiber_ 执行
    }

    std::vector<Thread::ThreadPtr> threads;
    {
        Mutex::Lock lock(mutex_);
        threads.swap(threadPool_); // 交换线程池，防止长时间阻塞
    }

    for(auto& thread : threads) {
        if(thread) {
            thread->join(); // 等待所有线程结束
        }
    }
}

}