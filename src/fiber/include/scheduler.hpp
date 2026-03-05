#pragma once

#include <string>
#include <memory>
#include <vector>
#include <list>
#include <atomic>
#include <mutex>

#include "utils.hpp"
#include "fiber.hpp"
#include "thread.hpp"
#include "mutex.hpp"

namespace monsoon {

class Scheduler;

class SchedulerTask {
public:
    friend class Scheduler;
    using FiberPtr = Fiber::FiberPtr;

    SchedulerTask() { threadId_ = -1; } // 默认线程亲和性为 -1，表示可以在任意线程上执行
    SchedulerTask(FiberPtr fiber, int threadId) 
                : fiber_(std::move(fiber)), threadId_(threadId) {}
    
    SchedulerTask(FiberPtr* fiber, int threadId){
        fiber_.swap(*fiber);
        threadId_ = threadId;
    }

    SchedulerTask(funcCallBack cb, int threadId)
                : cb_(std::move(cb)), threadId_(threadId) {}

    // 清空任务
    void reset() {
        fiber_.reset();
        cb_ = nullptr;
        threadId_ = -1;
    }

private:
    FiberPtr fiber_;    // 任务可以直接是一个协程对象
    funcCallBack cb_;  // 也可以是一个回调函数

    // 线程亲和性：-1表示该任务可以在任意线程上执行；
    // 如果 >= 0，表示该任务必须在指定 ID 的线程上执行
    int threadId_;
};


class Scheduler {
public:
    using SchedulerPtr = std::shared_ptr<Scheduler>;

    Scheduler(size_t threadCnt = 1, bool useCaller = true, const std::string& name = "Scheduler");
    virtual ~Scheduler();

    const std::string& getName() const;

    // 获取当前线程的调度器协程
    static Fiber* getMainFiber();

    // 获取当前线程调度器
    static Scheduler* getThis();

    void start();

    void stop();

    // 添加调度任务
    // TaskType 任务类型，可以是协程对象或函数指针
    template<typename TaskType>
    void scheduler(TaskType task, int threadId = -1) {
        bool isTrickle = false;
        {
            Mutex::Lock lock(mutex_);
            isTrickle = isNeedTrickle(task, threadId);
        }

        if(isTrickle) {
            trickle(); // 唤醒idle协程
        }
    }

protected:
    void setThis();

    virtual void idle();

    virtual void trickle();

    virtual bool stopping();

    void run();

    // 返回是否有空闲进程
    bool isHasIdleThreads() const { return idleThreadCnt_ > 0; }

private:
    // 判断任务是否需要唤醒 idle 协程
    template<typename TaskType>
    bool isNeedTrickle(TaskType task, int threadId) {
        bool isNeedTrick = tasks_.empty();

        SchedulerTask cur_task(task, threadId);
        if(cur_task.fiber_ || cur_task.cb_) {
            tasks_.push_back(std::move(cur_task));
        }
        return isNeedTrick;
    }

    // rootFiber_ 是调度器的主协程，他和线程的主协程 (cur_thread_fiber) 是两个不同的协程
    Fiber::FiberPtr rootFiber_;

    std::string name_; // 调度器名称
    Mutex mutex_;
    std::list<SchedulerTask> tasks_; // 任务队列
    std::vector<Thread::ThreadPtr> threadPool_; // 线程池
    std::vector<int> threadIds_; // 线程 ID 列表
    bool isStopped_; // 是否停止调度器

    size_t threadCnt_ = 0; // 线程数量
    std::atomic<size_t> activeThreadCnt_ = {0}; // 活跃线程数量
    std::atomic<size_t> idleThreadCnt_ = {0}; // 空闲线程数量

    bool isUserCaller_ ;    // isUserCaller_ = true,调度器所在线程的协程作为调度协程
    int rootThreadId_ = 0;  // isUserCaller_ = true,调度器协程所在线程的id
};

}