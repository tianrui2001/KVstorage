#pragma once

#include <memory>
#include <stdint.h>
#include <set>
#include <vector>

#include "utils.hpp"
#include "mutex.hpp"

namespace monsoon {

class TimerManager;

// 基于红黑树 管理的定时器系统
class Timer : public std::enable_shared_from_this<Timer>
{
public:
    using TimerPtr = std::shared_ptr<Timer>;

    bool cancel();
    bool refresh();
    bool reset(uint64_t ms, bool from_now);

private:
    friend class TimerManager;

    Timer(uint64_t next);
    Timer(uint64_t ms, bool is_repeat, funcCallBack cb, TimerManager* mgr);
    
    struct Comparator {
        bool operator()(const TimerPtr& lhs, const TimerPtr& rhs) const;
    };

    bool is_repeat_ = false;        // 是否重复
    uint64_t ms_ = 0;       // 执行周期
    uint64_t next_ = 0;     // 下一次的执行时间
    funcCallBack cb_;       // 定时器回调函数
    TimerManager* manager_ = nullptr;   // 指向定时器管理器的指针
};

class TimerManager {
public:
    using TimerPtr = Timer::TimerPtr;
    using TimerComparator = Timer::Comparator;

    TimerManager();
    ~TimerManager();

    TimerPtr addTimer(uint64_t ms, funcCallBack cb, bool is_repeat = false);
    TimerPtr addConditionTimer(uint64_t ms, funcCallBack cb, 
                                std::weak_ptr<void> weak_cond, bool is_repeat = false);

    // 到最近一个定时器的时间间隔（ms）
    uint64_t getNextTimer();
    // 处理过期定时器 ,获取需要执行的定时器的回调函数列表
    void listExpiredCb(std::vector<funcCallBack>& cbs);
    // 是否有定时器
    bool hasTimer();
protected:
    // 当有新的定时器被插入到定时器集合的前面时，触发该函数。在IOManager里面实现
    virtual void OnTimerInsertedAtFront() = 0;   
    // 将定时器添加到管理器
    void addTimer(TimerPtr timer, RWMutex::WriteLock& lock);

private:
    bool detectClockRollover(uint64_t now_ms);

    RWMutex mutex_;
    std::set<TimerPtr, TimerComparator> timers_;   // 定时器集合
    bool tickled_ = false;   // 是否触发OnTimerInsertedAtFront
    uint64_t previousTime_ = 0;   // 上一次执行的时间
    
    friend class Timer;
};
}