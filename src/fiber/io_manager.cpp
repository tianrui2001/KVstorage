
#include "utils.hpp"
#include "io_manager.hpp"

namespace monsoon {

EventContext& FdContext::getContext(Event event){
    switch(event) {
        case READ:
            return read_;
        case WRITE:
            return write_;
        default:
            CondPanic(false, "getContext error");
    }
}

void FdContext::resetContext(EventContext& ctx){
    ctx.scheduler_ = nullptr;
    ctx.fiber_.reset();
    ctx.cb_ = nullptr;
}

void FdContext::triggerEvent(Event event){
    if(event & events_ == NONE) {
        return; // 没有注册这个事件
    }

    // 清空这个事件
    events_ = (Event)(events_ & ~event);
    EventContext& ctx = getContext(event);
    if(ctx.cb_) {
        ctx.scheduler_->scheduler(ctx.cb_); // 调度回调函数执行
    } else {
        ctx.scheduler_->scheduler(ctx.fiber_); // 调度协程执行
    }

    resetContext(ctx); // 重置事件上下文
}


IOManager::IOManager(size_t threadCnt, bool useCaller, const std::string& name)
    : Scheduler(threadCnt, useCaller, name)
{
    epfd_ = epoll_create(10);
    CondPanic(epfd_ < 0, "epoll_create failed");

    int ret = pipe(tickleFds_);
    CondPanic(ret != 0, "pipe failed");

    epoll_event event;
    memset(&event, 0, sizeof(epoll_event));
    event.data.fd = tickleFds_[0];    // 监听管道的读端
    event.events = EPOLLIN | EPOLLET; // 边缘触发模式
    ret = fcntl(tickleFds_[0], F_SETFL, O_NONBLOCK);
    CondPanic(ret != 0, "set fd nonblock error");

    ret = epoll_ctl(epfd_, EPOLL_CTL_ADD, tickleFds_[0], &event);
    CondPanic(ret != 0, "epoll_ctl failed");

    contextResize(32); // 初始化上下文数组大小为32

    start(); // 启动调度器
}

IOManager::~IOManager() {
    stop(); // 停止调度器

    close(epfd_); // 关闭 epoll 文件描述符
    close(tickleFds_[0]); // 关闭管道读端
    close(tickleFds_[1]); // 关闭管道写端

    for(auto& fdCtx : fdContexts_) {
        if(fdCtx) {
            delete fdCtx; // 删除文件描述符上下文
        }
    }
}

int IOManager::addEvent(int fd, Event event, funcCallBack cb = nullptr){
    FdContext* fdCtx = nullptr;
    RWMutex::ReadLock lock(mutex_);

    // 找到fd对应的fdCOntext,没有则创建
    if(fd < fdContexts_.size()) {
        fdCtx = fdContexts_[fd];
        lock.unlock();
    } else {
        lock.unlock();
        RWMutex::WriteLock wrLock(mutex_);
        contextResize(fd * 1.5);
        fdCtx = fdContexts_[fd];
    }

    // 构造一个 epoll event， 并注册到 epoll 中
    Mutex::Lock fdCtxLock(fdCtx->mutex_);
    CondPanic(!(fdCtx->events_ & event), "event already registered"); // 已经注册的事件不能重复注册
    epoll_event epevent;
    epevent.data.ptr = fdCtx;
    epevent.events = EPOLLET | fdCtx->events_ | event; // 边缘触发模式
    int op = fdCtx->events_? EPOLL_CTL_MOD : EPOLL_CTL_ADD;

    int ret = epoll_ctl(epfd_, op, fd, &epevent);
    if (ret) {
        std::cout << "addevent: epoll ctl error" << std::endl;
        return -1;
    }

    pendingEventCnt_++;
    fdCtx->events_ = (Event)(fdCtx->events_ | event); // 更新已注册事件
    EventContext& ctx = fdCtx->getContext(event);
    if(cb) {
        ctx.cb_.swap(cb); // 如果有回调函数，保存到事件上下文
    } else {
        ctx.scheduler_ = Scheduler::getThis(); // 否则，保存当前调度
        CondPanic(ctx.fiber_->getState() == Fiber::State::RUNNING, "fiber is running");
    }

    std::cout << "add event success,fd = " << fd << std::endl;
    return 0;
}

bool IOManager::delEvent(int fd, Event event){
    RWMutex::ReadLock lock(mutex_);
    if(fd >= fdContexts_.size()){
        return false; // 文件描述符无效
    }

    FdContext* fdCtx = fdContexts_[fd];
    lock.unlock();

    Mutex::Lock fdCtxLock(fdCtx->mutex_);
    if(!(fdCtx->events_ & event)){
        return false; // 没有注册这个事件
    }

    auto newEvents = (Event)(fdCtx->events_ & ~event); // 移除事件
    int op = newEvents ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.data.ptr = fdCtx;
    epevent.events = EPOLLET | newEvents; // 边缘触发
    int ret = epoll_ctl(epfd_, op, fd, &epevent);
    if (ret) {
        std::cout << "delevent: epoll ctl error" << std::endl;
        return false;
    }

    pendingEventCnt_--;
    fdCtx->events_ = newEvents; // 更新已注册事件
    EventContext& ctx = fdCtx->getContext(event);
    fdCtx->resetContext(ctx); // 重置事件上下文
    return true;
}

bool IOManager::cancelEvent(int fd, Event event){
    RWMutex::ReadLock lock(mutex_);
    if(fd >= fdContexts_.size()){
        return false; // 文件描述符无效
    }

    FdContext* fdCtx = fdContexts_[fd];
    lock.unlock();

    Mutex::Lock fdCtxLock(fdCtx->mutex_);
    if(!(fdCtx->events_ & event)){
        return false; // 没有注册这个事件
    }

    auto newEvents = (Event)(fdCtx->events_ & ~event); // 移除事件
    int op = newEvents ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.data.ptr = fdCtx;
    epevent.events = EPOLLET | newEvents; // 边缘触发
    int ret = epoll_ctl(epfd_, op, fd, &epevent);
    if (ret) {
        std::cout << "delevent: epoll ctl error" << std::endl;
        return false;
    }

    // 删除之前，触发以此事件，强制唤醒等待该事件的协程，让它知道事件被取消了
    fdCtx->triggerEvent(event);
    pendingEventCnt_--;
    return true;
}

bool IOManager::cancelAll(int fd){
    RWMutex::ReadLock lock(mutex_);
    if(fd >= fdContexts_.size()){
        return false; // 文件描述符无效
    }

    FdContext* fdCtx = fdContexts_[fd];
    lock.unlock();
    Mutex::Lock fdCtxLock(fdCtx->mutex_);
    if(fdCtx->events_ == NONE){
        return false; // 没有注册任何事件
    }

    int op =  EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.data.ptr = fdCtx;
    epevent.events = 0;
    int ret = epoll_ctl(epfd_, op, fd, &epevent);
    if (ret) {
        std::cout << "delevent: epoll ctl error" << std::endl;
        return false;
    }

    if(fdCtx->events_ & READ) {
        fdCtx->triggerEvent(READ);
        pendingEventCnt_--;
    } else if(fdCtx->events_ & WRITE) {
        fdCtx->triggerEvent(WRITE);
        pendingEventCnt_--;
    }

    CondPanic(fdCtx->events_ == NONE, "cancel all events error");
    return true;
}

IOManager* IOManager::getThis(){
    return dynamic_cast<IOManager*>(Scheduler::getThis());
}

void IOManager::contextResize(size_t size){
    fdContexts_.resize(size);
    for(size_t i=0; i<size; ++i) {
        if(!fdContexts_[i]) {
            fdContexts_[i] = new FdContext();
            fdContexts_[i]->fd_ = i; // 设置文件描述符为索引
        }
    }
}

// 调度器无任务则阻塞在idle线程上
// 当有新事件触发，则退出idle状态，则执行回调函数
// 当有新的调度任务，则退出idle状态，并执行对应任务
void IOManager::idle(){
    const size_t MAX_EVENTS_CNT = 256; // 最多只能检测256个事件
    const int MAX_TIMEOUT = 5000; // 最大超时时间为5秒

    epoll_event* events = new epoll_event[MAX_EVENTS_CNT]();
    std::shared_ptr<epoll_event> eventsPtr(events, [](epoll_event* ptr){
        delete[] ptr;
    });

    while(true){
        uint64_t next_timeout = 0;
        if(stopping(next_timeout)) {    // 判断是否可以停止，并获取最近一个定时超时时间
            std::cout << "name=" << getName() << " idle stopping exit" << std::endl;
            return; // 停止调度器
        }

        int ret = 0;
        while(true) {
            if(next_timeout != ~0ull) {
                next_timeout = std::min((int)next_timeout, MAX_TIMEOUT);
            } else {
                next_timeout = MAX_TIMEOUT;
            }

            ret = epoll_wait(epfd_, events, MAX_EVENTS_CNT, next_timeout);
            if(ret < 0){
                if(errno == EINTR) {
                    continue; // 被信号中断，重新等待
                }

                std::cout << "epoll_wait error, ret = " << ret << std::endl;
                break; // 其他错误，退出循环
            } else {
                break;
            }
        }
        
        // 1. 检查是不是闹钟响了，listExpiredCb 会把所有到期的定时器回调取出来
        // 如果有，就把这些定时器的回调函数通过 scheduler(cb) 扔回任务队列
        std::vector<funcCallBack> cbs;
        listExpiredCb(cbs); // 获取过期的定时器回调函数列表
        if(!cbs.empty()) {
            for(auto& cb : cbs) {
                scheduler(cb); // 将 cb 加入到scheduler的任务队列中
            }
            cbs.clear(); // 清空回调函数列表
        }

        // 2. 检查是不是电话(普通 IO 事件)或tickle
        for(int i=0; i<ret; ++i) {
            epoll_event& event = events[i];

            // a. 如果是 tickle 事件：从管道里读出数据清空管道，然后 continue 处理下一个事件
            if(event.data.fd == tickleFds_[0]) {
                uint8_t dummy;
                while(read(tickleFds_[0], &dummy, 1) > 0); // 清空管道
                continue;
            }

            //  b. 如果是电话(普通 IO 事件)
            FdContext* fdCtx = (FdContext*)event.data.ptr;
            Mutex::Lock lock(fdCtx->mutex_);

            if(event.events & (EPOLLERR | EPOLLHUP)){   // 错误处理
                std::cout << "epoll error, fd = " << fdCtx->fd_ << std::endl;
                event.events = ((EPOLLIN | EPOLLOUT) & fdCtx->events_); // 只保留读写事件
            }

            int realEvents = NONE;
            if(event.events & EPOLLIN) realEvents |= READ;
            if(event.events & EPOLLOUT) realEvents |= WRITE;
            if(realEvents == NONE || !(fdCtx->events_ & realEvents)) {
                continue; // 没有需要处理的事件, 或者是注册的事件和触发的事件无交集
            }

            int leftEvents = fdCtx->events_ & ~realEvents; // 剩余的事件
            int op = leftEvents ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events = EPOLLET | leftEvents;
            int ret = epoll_ctl(epfd_, op, fdCtx->fd_, &event);
            if(ret) {
                std::cout << "epoll_wait [" << epfd_ << "] errno,err: " << errno << std::endl;
                continue; // epoll_ctl 失败，继续处理下一个事件
            }

            if(realEvents & READ) {
                fdCtx->triggerEvent(READ);
                pendingEventCnt_--;
            }
            if(realEvents & WRITE) {
                fdCtx->triggerEvent(WRITE);
                pendingEventCnt_--;
            }
        }
        
        // 所有就绪的事件都处理完毕后，idle 协程让出执行权。
        // 处理结束，idle协程yield,此时调度协程可以执行run去tasklist中
        // 检测，拿取新任务去调度
        Fiber::FiberPtr cur_fiber = Fiber::getThis();
        auto raw_ptr = cur_fiber.get();
        cur_fiber.reset(); // 重置当前协程，复用栈空间，防止yield时的循环引用
        raw_ptr->yield();
    }
}

// 打破 idle() 中的 epoll_wait 阻塞。
// 确保了无论是新任务还是新定时器，
// 都能让沉睡的调度线程立即响应，而不是等到 epoll_wait 超时
void IOManager::trickle(){
    if(!isHasIdleThreads()){
        return; // 没有空闲线程
    }

    int ret = write(tickleFds_[1], "T", 1);
    CondPanic(ret == 1, "write tickle pipe error");
}

// 判断是否可以停止
bool IOManager::stopping(){
    uint64_t timeout = 0;
    return stopping(timeout);
}

void IOManager::OnTimerInsertedAtFront(){
    trickle(); // 当有新的定时器被插入到定时器集合的前面时，唤醒 idle 协程
}

// 判断是否可以停止，同时获取最近一个定时超时时间
bool IOManager::stopping(uint64_t& timeout){
    // 所有待调度的Io事件执行结束后，才允许退出
    timeout = getNextTimer();
    return timeout == ~0ull && pendingEventCnt_ == 0 && Scheduler::stopping();
}

}