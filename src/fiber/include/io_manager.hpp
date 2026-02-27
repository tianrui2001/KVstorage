#pragma once

#include <fcntl.h>
#include <string.h>
#include <sys/epoll.h>

#include "scheduler.hpp"
#include "mutex.hpp"
#include "timer.hpp"

namespace monsoon {

enum Event {
    NONE = 0x0,
    READ = 0x1,
    WRITE = 0x4,
};

struct EventContext
{
    Scheduler* scheduler_ = nullptr; // 事件执行的调度器
    Fiber::FiberPtr fiber_;          // 事件执行的协程
    funcCallBack cb_;                // 事件执行的回调函数
};

class FdContext
{
public:
    friend class IOManager;

    EventContext& getContext(Event event); // 获取事件上下文
    void resetContext(EventContext& ctx); // 重置事件上下文
    void triggerEvent(Event event); // 触发事件

private:
    EventContext read_;  // 读事件
    EventContext write_; // 写事件
    int fd_;            // 文件描述符
    Event events_ = NONE; // 已注册的事件
    Mutex mutex_;       // 保护事件上下文的互斥锁
};

class IOManager : public Scheduler, public TimerManager
{
public:
    using IOManagerPtr = std::shared_ptr<IOManager>;

    IOManager(size_t threadCnt = 1, bool useCaller = true, const std::string& name = "IOManager");
    ~IOManager();

    int addEvent(int fd, Event event, funcCallBack cb = nullptr);
    bool delEvent(int fd, Event event);
    bool cancelEvent(int fd, Event event);
    bool cancelAll(int fd);
    static IOManager* getThis(); // 获取当前线程的 IO 管理器

protected:
    void contextResize(size_t size); // 调整上下文数组大小

    // 继承自 Scheduler 的虚函数
    void idle() override; // 空闲时执行的函数
    void trickle() override; // 唤醒 idle 协程
    bool stopping() override; // 停止调度器
    void OnTimerInsertedAtFront() override; // 当有新的定时器被插入到定时器集合的前面时触发

    bool stopping(uint64_t& timeout); // 停止 IO 管理器

private:
    int epfd_; // epoll 文件描述符
    int tickleFds_[2]; // 一个管道的两个文件描述符
    std::atomic<size_t> pendingEventCnt_ = {0}; // 待处理事件数量
    RWMutex mutex_;
    std::vector<FdContext*> fdContexts_; // 文件描述符上下文数组，索引是文件描述符
};

}