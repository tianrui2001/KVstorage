#pragma once

#include <pthread.h>
#include <mutex>
#include <iostream>

#include "utils.hpp"
#include "noncopyable.hpp"

namespace monsoon {

// 局部锁类模板
template <typename T>
class ScopedLockImpl {
public:
    ScopedLockImpl(T& mutex) :mutex_(mutex) {
        mutex_.lock();
        isLocked_ = true;
    }

    ~ScopedLockImpl() { unlock(); }

    void lock() {
        if(!isLocked_) {
            std::cout << "lock" << std::endl;
            mutex_.lock();
            isLocked_ = true;
        }
    }

    void unlock() {
        if(isLocked_) {
            mutex_.unlock();
            isLocked_ = false;
        }
    }

private:
    T& mutex_;
    bool isLocked_;
};

// 读锁类模板
template <typename T>
class ReadScopedLockImpl {
public:
    ReadScopedLockImpl(T& mutex) :mutex_(mutex) {
        mutex_.rdlock();
        isLocked_ = true;
    }

    ~ReadScopedLockImpl() { unlock(); }

    void lock() {
        if(!isLocked_) {
            mutex_.rdlock();
            isLocked_ = true;
        }
    }

    void unlock() {
        if(isLocked_) {
            mutex_.unlock();
            isLocked_ = false;
        }
    }

private:
    T& mutex_;
    bool isLocked_;
};

// 写锁类模板
template <typename T>
class WriteScopedLockImpl {
public:
    WriteScopedLockImpl(T& mutex) :mutex_(mutex) {
        mutex_.wrlock();
        isLocked_ = true;
    }

    ~WriteScopedLockImpl() { unlock(); }
    void lock() {
        if(!isLocked_) {
            mutex_.wrlock();
            isLocked_ = true;
        }
    }

    void unlock() {
        if(isLocked_) {
            mutex_.unlock();
            isLocked_ = false;
        }
    }

private:
    T& mutex_;
    bool isLocked_;
};

// 互斥锁类, 通过调用Lock.lock()和Lock.unlock()来加锁和解锁
class Mutex : Noncopyable
{
public:
    using Lock = ScopedLockImpl<Mutex>;

    Mutex() {CondPanic(0 == pthread_mutex_init(&mutex_, nullptr), "lock init success"); }
    ~Mutex() { CondPanic(0 == pthread_mutex_destroy(&mutex_), "destroy lock error"); }

    void lock() { CondPanic(0 == pthread_mutex_lock(&mutex_), "lock error"); }
    void unlock() { CondPanic(0 == pthread_mutex_unlock(&mutex_), "unlock error"); }

private:
    pthread_mutex_t mutex_;
};

// 读写锁类, 通过调用ReadLock.lock()和ReadLock.unlock()来加读锁, 
// 调用WriteLock.lock()和WriteLock.unlock()来加写锁
class RWMutex : Noncopyable
{
public:
    using ReadLock = ReadScopedLockImpl<RWMutex>;
    using WriteLock = WriteScopedLockImpl<RWMutex>;

    RWMutex() { pthread_rwlock_init(&rw_mutex_, nullptr); }
    ~RWMutex() { pthread_rwlock_destroy(&rw_mutex_); }

    void rdlock() { pthread_rwlock_rdlock(&rw_mutex_); }
    void wrlock() { pthread_rwlock_wrlock(&rw_mutex_); }
    void unlock() { pthread_rwlock_unlock(&rw_mutex_); }

private:
    pthread_rwlock_t rw_mutex_;
};

}