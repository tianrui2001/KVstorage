#include "timer.hpp"
#include "utils.hpp"

namespace monsoon {

// 定时器比较器，按照定时器的下次执行时间进行比较。左小右大
bool Timer::Comparator::operator()(const TimerPtr& lhs, const TimerPtr& rhs) const {
    if(!lhs && !rhs) {
        return false;
    }

    if(!lhs) return true;
    if(!rhs) return false;

    if(lhs->next_ == rhs->next_) {
        return lhs.get() < rhs.get();
    } else {
        return lhs->next_ < rhs->next_;
    }
}

Timer::Timer(uint64_t next) :next_(next) {}

Timer::Timer(uint64_t ms, bool is_repeat, funcCallBack cb, TimerManager* mgr)
    : is_repeat_(is_repeat), ms_(ms), cb_(cb), manager_(mgr)
{
    next_ = monsoon::GetElapsedMS() + ms_;  // 下一次的执行时间 = 当前时间 + 执行周期
}

bool Timer::cancel(){
    RWMutex::WriteLock lock(manager_->mutex_);
    if(cb_) {
        cb_ = nullptr;  // 将回调函数置空，等待销毁
        auto it = manager_->timers_.find(shared_from_this());
        if(it != manager_->timers_.end()) {
            manager_->timers_.erase(it);
            return true;
        }
    }

    return false;
}

bool Timer::refresh(){
    RWMutex::WriteLock lock(manager_->mutex_);
    if(!cb_) {
        return false;
    }

    auto it = manager_->timers_.find(shared_from_this());
    if(it == manager_->timers_.end()) {
        return false;
    }

    manager_->timers_.erase(it);  // 从管理器中删除定时器
    next_ = monsoon::GetElapsedMS() + ms_;  // 更新下一次到期时间为当前时间 + 执行周期
    manager_->timers_.insert(shared_from_this());  // 重新插入管理器
    return true;
}

// 重置定时器，重新设置定时器触发时间
// from_now = true: 下次出发时间从当前时刻开始计算
// from_now = false: 下次触发时间从上一次开始计算
bool Timer::reset(uint64_t ms, bool from_now){
    if(ms == ms_ && !from_now) {
        return true;  // 如果执行周期没有改变，并且不是从现在开始，直接返回
    }

    RWMutex::WriteLock lock(manager_->mutex_);
    if(!cb_) {
        return false;
    }

    auto it = manager_->timers_.find(shared_from_this());
    if(it == manager_->timers_.end()) {
        return false;
    }
    manager_->timers_.erase(it);
    uint64_t start_tm = from_now ? monsoon::GetElapsedMS() : next_ - ms_;
    ms_ = ms;
    next_ = start_tm + ms_;
    manager_->addTimer(shared_from_this(), lock);  // 重新添加到管理器中
    return true;
}


TimerManager::TimerManager() { previousTime_ = monsoon::GetElapsedMS(); }

TimerManager::~TimerManager() {}

TimerManager::TimerPtr TimerManager::addTimer(uint64_t ms, funcCallBack cb, bool is_repeat = false){
    TimerPtr timer(new Timer(ms, is_repeat, cb, this));
    RWMutex::WriteLock lock(mutex_);
    addTimer(timer, lock);
    return timer;
}

static void onTimer(std::weak_ptr<void> weak_cond, funcCallBack cb) {
    std::shared_ptr<void> tmp = weak_cond.lock();
    if(tmp) {
        // 如果提升成功，说明绑定的对象还活着，执行回调
        cb();
    }
}

TimerManager::TimerPtr TimerManager::addConditionTimer(uint64_t ms, 
                                                        funcCallBack cb, 
                                                        std::weak_ptr<void> weak_cond, 
                                                        bool is_repeat){
    return addTimer(ms, std::bind(&onTimer, weak_cond, cb), is_repeat);
}

void TimerManager::addTimer(TimerPtr timer, RWMutex::WriteLock& lock){
    auto it = timers_.insert(timer).first; // 返回的是一个pair，first是插入元素的迭代器
    bool at_front = (it == timers_.begin()) && !tickled_; // 判断是否插入到最前面，并且没有触发OnTimerInsertedAtFront
    if(at_front) {
        tickled_ = true;
    }

    lock.unlock();
    if(at_front) {
        // IOManager 继承自 TimerManager 以及 Scheduler，所以可以调用
        // 原本是使用epoll等待的，但是最近的唤醒时间变了，调用虚函数去唤醒调度器，就不使用epoll的超时时间了
        OnTimerInsertedAtFront();
    }
}

uint64_t TimerManager::getNextTimer(){
    RWMutex::ReadLock lock(mutex_);
    tickled_ = false;

    if(timers_.empty()){
        return ~0ull;  // 返回无符号整数的最大值，表示没有定时器
    }

    const TimerPtr& next = *timers_.begin(); // 获取第一个定时器
    uint64_t now = monsoon::GetElapsedMS();

    if(now >= next->next_) {
        return 0;   // 已经过期了，立即执行
    } else {
        return next->next_ - now;  // 返回距离下一个定时器的时间间隔
    }
}

void TimerManager::listExpiredCb(std::vector<funcCallBack>& cbs){
    uint64_t now_ms = monsoon::GetElapsedMS();
    std::vector<TimerPtr> expireds;
    {
        RWMutex::ReadLock lock(mutex_);
        if(timers_.empty()) {
            return;
        }
    }

    RWMutex::WriteLock lock(mutex_);
    if(timers_.empty()) {
        return;
    }

    // 检测是否有时钟回拨
    bool isRollover = false;
    if(detectClockRollover(now_ms)){
        isRollover = true;
    }

    // 如果没有回拨，且最近的一个定时器还没到时间，直接返回
    if(!isRollover && ((*timers_.begin())->next_ > now_ms)){
        return;
    }

    // 如果没有回拨，且最近的一个定时器还没到时间，直接返回
    TimerPtr now_timer(new Timer(now_ms));
    auto it = isRollover ? timers_.end() : timers_.lower_bound(now_timer);
    while(it != timers_.end() && (*it)->next_ <= now_ms) {
        it++;
    }

    // 提取过期定时器，将 [begin, it) 区间内的定时器移动到 expired 数组，并从管理器中删除这些定时器
    expireds.insert(expireds.begin(), timers_.begin(), it);
    timers_.erase(timers_.begin(), it);
    cbs.reserve(expireds.size());
    for(auto& timer : expireds) {
        cbs.push_back(timer->cb_);  // 将过期定时器的回调函数添加到 cbs 数组中
        if(timer->is_repeat_) { // 将重复的定时器重新添加到管理器中
            timer->next_ = now_ms + timer->ms_;
            timers_.insert(timer);
        } else {
            timer->cb_ = nullptr;  // 非重复定时器，清空回调函数，等待销毁
        }
    }
}

bool TimerManager::hasTimer(){
    RWMutex::ReadLock lock(mutex_);
    return !timers_.empty();
}

bool TimerManager::detectClockRollover(uint64_t now_ms) {
    // 如果当前时间比上次记录的时间小，并且小了超过 1 小时
    // 这里 60*60*1000 是个经验值，防止微小的抖动被误判
    if(now_ms < previousTime_ && now_ms < (previousTime_ -60*60*1000)) {
        return true;
    }

    previousTime_ = now_ms; // 更新上次执行时间
    return false;
}
}