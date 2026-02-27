#include <dlfcn.h>
#include <stdint.h>
#include <string>
#include <stdarg.h>

#include "hook.hpp"
#include "fd_manager.hpp"
#include "io_manager.hpp"

namespace monsoon {
static thread_local bool hookEnabled = false;

bool isHookEnable(){
    return hookEnabled;
}
void setHookEnable(const bool flage) {
    hookEnabled = flage;
}

// 这是一个 X Macro，一种高级宏用法。它的作用是避免重复写代码。
// 所有需要被 Hook 的函数名都在这里列出。`XX` 是一个占位符,将宏作为参数。
#define HOOK_FUNC(XX) \
  XX(sleep)          \
  XX(usleep)         \
  XX(nanosleep)      \
  XX(socket)         \
  XX(connect)        \
  XX(accept)         \
  XX(read)           \
  XX(readv)          \
  XX(recv)           \
  XX(recvfrom)       \
  XX(recvmsg)        \
  XX(write)          \
  XX(writev)         \
  XX(send)           \
  XX(sendto)         \
  XX(sendmsg)        \
  XX(close)          \
  XX(fcntl)          \
  XX(ioctl)          \
  XX(getsockopt)     \
  XX(setsockopt)

void hook_init() {
    static bool isInit = false;   // 防止重复初始化
    if(isInit) {
        return; // 已经 Hook 过了
    }
    isInit = true;

// 定义一个代码生成器 XX
#define XX(name) name##_f = (name##_fun)dlsym(RTLD_NEXT, #name);
    HOOK_FUNC(XX);  // 把刚刚定义的代码生成器传进去
#undef XX   // 用完就扔，防止污染
}

// hook_init放在静态对象中，则在main函数执行之前静态对象的构造函数就会运行
static uint64_t s_connect_timeout = -1;
struct _HOOKIniter {
    _HOOKIniter() {
        hook_init(); // 初始化 Hook
        s_connect_timeout = 5000; // 设置默认连接超时时间为 5 秒
    }
};
static _HOOKIniter s_hook_initer; // 静态对象，确保在程序启动时就初始化 Hook

struct timer_info
{
    int cancelled = 0;
};


template<typename OriginakFunc, typename... Args>
static size_t do_io(int fd, OriginakFunc func, 
                    const std::string hookFuncName, 
                    Event event, int timeout, Args&&... args){
    if(!isHookEnable()) {
        return func(fd, std::forward<Args>(args)...);
    }

    FdCtx::FdCtxPtr fdCtx = FdManager::getInstance().get(fd);
    if(!fdCtx || !fdCtx->isSocket() || fdCtx->getUserNonblock()) {
        return func(fd, std::forward<Args>(args)...);
    }

    if(fdCtx->isClosed()) {
        errno = EBADF;
        return -1; // 文件描述符已关闭
    }

    // 获取对应type的fd超时时间
    uint64_t timeoutMs = fdCtx->getTimeout(event);
    std::shared_ptr<timer_info> timerInfo(new timer_info());
    ssize_t n;

    while(true) {
        n = func(fd, std::forward<Args>(args)...);
        while(n == -1  && errno == EINTR) { // 被信号中断，重新调用
            n = func(fd, std::forward<Args>(args)...); 
        }

        if(n != -1 || errno != EAGAIN) {
            return n; // 成功了，或者发生了非 EAGAIN 错误了
        }

        // 异步等待 - “数据没好，我去睡会，好了叫我”
        IOManager* iom = IOManager::getThis();
        Timer::TimerPtr timer;
        std::weak_ptr<timer_info> wk_tinfo(timerInfo);

        if(timeoutMs != (uint64_t)-1) {
            timer = iom->addConditionTimer(timeoutMs,
                            [wk_tinfo, fd, iom, event](){
                                auto tinfo = wk_tinfo.lock();
                                if(tinfo->cancelled) {
                                    return; // 定时器已经被取消了,防止重复取消
                                }
                                tinfo->cancelled = ETIMEDOUT; // 超时了
                                iom->cancelEvent(fd, event); // 取消事件
                            }, 
                            wk_tinfo);
        }

        // b. 向 epoll 注册 IO 事件
        // 告诉 IOManager：“请帮我监听 fd 的读/写事件，好了叫我”
        int ret = iom->addEvent(fd, event);
        if(ret) {
            if(timer) {
                timer->cancel(); // 注册事件失败，取消定时器
            }
            return -1;
        } else {
            Fiber::getThis()->yield(); // 让出协程执行权，等待事件触发

            // ----------- 协程被唤醒后，从这里继续执行 -----------
            if(timer) { // 不论是定时器唤醒的还是IO事件唤醒的，都需要取消定时器
                timer->cancel(); // d. 清理定时器
            }

            if(timerInfo->cancelled) {
                errno = timerInfo->cancelled; // 超时了，设置 errno
                return -1;
            }
        }
    }

    return n;
}

extern "C" {
#define XX(name) name##_fun name##_f = nullptr;
    HOOK_FUNC(XX);  // 定义函数指针变量
#undef XX

// 睡眠多少秒
unsigned int sleep(unsigned int seconds) {
    if(!isHookEnable()) {
        return sleep_f(seconds); // 调用原始的 sleep 函数
    }

    Fiber::FiberPtr cur_fiber = Fiber::getThis();
    IOManager* iom = IOManager::getThis();

    // &IOManager::scheduler：函数地址。直接取地址是不行的，因为 scheduler 是一个模板函数
    // (void(Scheduler::*)(Fiber::ptr, int thread))：类型转换，Scheduler::*：表示这是一个指向 Scheduler 类成员的指针
    iom->addTimer(seconds * 1000, 
        std::bind((void(Scheduler::*)(Fiber::FiberPtr, int thread))&IOManager::scheduler, 
        iom, cur_fiber, -1));
    
    cur_fiber->yield();
    return 0;
}

// usleep 在指定的微妙数内暂停线程运行
int usleep(useconds_t usec) {
    if(!isHookEnable()) {
        return usleep_f(usec);
    }

    Fiber::FiberPtr cur_fiber = Fiber::getThis();
    IOManager* iom = IOManager::getThis();
    iom->addTimer(usec / 1000,
        std::bind((void(Scheduler::*)(Fiber::FiberPtr, int thread))&IOManager::scheduler, 
        iom, cur_fiber, -1));
    cur_fiber->yield();
    return 0;
}

// nanosleep 在指定的纳秒数内暂停当前线程的执行
int nanosleep(const struct timespec *req, struct timespec *rem) {
    if(!isHookEnable()) {
        return nanosleep_f(req, rem);
    }

    Fiber::FiberPtr cur_fiber = Fiber::getThis();
    IOManager* iom = IOManager::getThis();
    iom->addTimer(req->tv_sec * 1000 + req->tv_nsec / 1000000,
        std::bind((void(Scheduler::*)(Fiber::FiberPtr, int thread))&IOManager::scheduler, 
        iom, cur_fiber, -1));
    cur_fiber->yield();
    return 0;
}

int socket(int domain, int type, int protocol) {
    if(!isHookEnable()) {
        return socket_f(domain, type, protocol);
    }

    int fd = socket_f(domain, type, protocol);
    if(fd == -1) {
        return -1; // 创建 socket 失败
    }

    // 为新 fd 建立档案
    // 将新创建的 fd 注册到 FdManager 中，并在内部自动将其设置为非阻塞模式
    FdManager::getInstance().get(fd, true);
    return fd;
}

// 实现带超时的非阻塞 connect
int connect_with_timeout(int fd, const struct sockaddr *addr, socklen_t addrlen, uint64_t timeout) {
    if(!isHookEnable()) {
        return connect_f(fd, addr, addrlen);
    }

    FdCtx::FdCtxPtr ctx = FdManager::getInstance().get(fd);
    if(!ctx || !ctx->isSocket() || ctx->getUserNonblock()) {
        return connect_f(fd, addr, addrlen);
    }

    if(ctx->isClosed()) {
        errno = EBADF;
        return -1; // 文件描述符已关闭
    }

    // 2. 首次尝试调用非阻塞 connect
    // 因为 fd 已经被 FdManager 设为非阻塞，所以 connect_f 会立即返回。
    int ret = connect_f(fd, addr, addrlen);
    if(ret == 0) {
        return 0; // 连接成功了
    } else if(ret != -1 || errno != EINPROGRESS) {
        return ret; // 连接失败了，且不是 EINPROGRESS 错误
    }

    // --- 如果返回 EINPROGRESS，说明连接正在后台进行，需要等待 ---
    // 3. 准备异步等待
    IOManager* iom = IOManager::getThis();
    Timer::TimerPtr timer;
    std::shared_ptr<timer_info> tinfo(new timer_info());
    std::weak_ptr<timer_info> wk_tinfo(tinfo);

    // 4. 设置超时定时器
    if(timeout != (uint64_t)-1) {
        timer = iom->addConditionTimer(timeout,
            [wk_tinfo, fd, iom](){
                auto info = wk_tinfo.lock();
                if(info->cancelled) {
                    return; // 定时器已经被取消了,防止重复取消
                }

                info->cancelled = ETIMEDOUT; // 超时了
                iom->cancelEvent(fd, Event::WRITE);
            },
            wk_tinfo);
    }
    
    // 5. 注册可写事件并让出
    // TCP 三次握手成功后，socket 会变为“可写”。
    // 所以我们监听 WRITE 事件来判断 connect 是否完成。
    ret = iom->addEvent(fd, Event::WRITE);
    if(ret == 0) {
        Fiber::getThis()->yield();

        // 等待超时or套接字可写，协程返回

        // a. 清理定时器
        if(timer) {
            timer->cancel();
        }

        // b. 检查是否超时
        if(tinfo->cancelled) {
            errno = tinfo->cancelled; // 超时了，设置 errno
            return -1;
        }
    } else {
        if(timer) {
            timer->cancel();
        }
        std::cout << "connect addEvent(" << fd << ", WRITE) error" << std::endl;
    }

    // 6. 检查 socket 错误状态
    // 协程被 IO 事件唤醒后，不代表 connect 百分百成功，可能对方 RST 了连接。
    // 必须使用 getsockopt(SOL_SOCKET, SO_ERROR) 来获取最终的连接结果。
    int err = 0;
    socklen_t len = sizeof(err);
    if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == -1){
        return -1; // getsockopt 调用失败了
    }

    if(err) {
        errno = err;
        return -1; // 连接失败了，err 是具体的错误码
    }
    return 0;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    return connect_with_timeout(sockfd, addr, addrlen, s_connect_timeout);
}

int accept(int listenfd, struct sockaddr* addr, socklen_t* addrlen) {
    // 1. 将 accept 逻辑委托给通用的 do_io 函数
    int fd = do_io(listenfd, accept_f, "accept", Event::READ, SO_RCVTIMEO, addr, addrlen);

    if(fd >= 0) {
        FdManager::getInstance().get(fd, true); // 为新 fd 建立档案，并设置为非阻塞
    }
    return fd;
}

ssize_t read(int fd, void* buf, size_t count){
    return do_io(fd, read_f, "read", Event::READ, SO_RCVTIMEO, buf, count);
}

ssize_t readv(int fd, const struct iovec* iov, int iovcnt) {
    return do_io(fd, readv_f, "readv", Event::READ, SO_RCVTIMEO, iov, iovcnt);
}

ssize_t recv(int fd, void* buf, size_t len, int flags) {
    return do_io(fd, recv_f, "recv", Event::READ, SO_RCVTIMEO, buf, len, flags);
}

ssize_t recvfrom(int fd, void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t* addrlen) {
    return do_io(fd, recvfrom_f, "recvfrom", Event::READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);

}

ssize_t recvmsg(int fd, struct msghdr* msg, int flags) {
    return do_io(fd, recvmsg_f, "recvmsg", Event::READ, SO_RCVTIMEO, msg, flags);
}

ssize_t write(int fd, const void* buf, size_t count) {
    return do_io(fd, write_f, "write", Event::WRITE, SO_SNDTIMEO, buf, count);
}

ssize_t writev(int fd, const struct iovec* iov, int iovcnt) {
    return do_io(fd, writev_f, "writev", Event::WRITE, SO_SNDTIMEO, iov, iovcnt);
}

ssize_t send(int fd, const void* buf, size_t len, int flags) {
    return do_io(fd, send_f, "send", Event::WRITE, SO_SNDTIMEO, buf, len, flags);
}

ssize_t sendto(int fd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen) {
    return do_io(fd, sendto_f, "sendto", Event::WRITE, SO_SNDTIMEO, buf, len, flags, dest_addr, addrlen);
}

ssize_t sendmsg(int fd, const struct msghdr* msg, int flags) {
    return do_io(fd, sendmsg_f, "sendmsg", Event::WRITE, SO_SNDTIMEO, msg, flags);
}

int close(int fd) {
    if(!isHookEnable()) {
        return close_f(fd);
    }

    FdCtx::FdCtxPtr fdCtx = FdManager::getInstance().get(fd);
    if(fdCtx) {
        auto iom = IOManager::getThis();
        if(iom) {
            // 2. 取消所有等待事件
            // 如果有其他协程正在等待这个 fd 的读/写事件，
            // 在关闭 fd 之前，必须把它们全部唤醒。
            // iom->cancelAll(fd) 会强制触发所有等待事件，
            // 让那些协程从 yield() 处醒来，并发现 fd 已关闭或出错。
            iom->cancelAll(fd); 
        }
        
        FdManager::getInstance().del(fd); // 删除 fd 上的上下文档案
    }
    return close_f(fd);
}

int fcntl(int fd, int cmd, ... /* args */) {
    va_list va;
    va_start(va, cmd);  // 初始化 va 这个“游标”。第二个参数必须是可变参数（...）之前的那个最后一个固定参数
    switch (cmd)
    {
    case F_SETFL:{
        int arg = va_arg(va, int); // 获取一个 int 参数
        va_end(va); // 清理工作。把 va_list 变量置为一个无效状态。某些架构必须调用，否则会导致内存泄漏
        FdCtx::FdCtxPtr fdCtx = FdManager::getInstance().get(fd);
        if(!fdCtx || fdCtx->isClosed() || fdCtx->getUserNonblock()){
            return fcntl_f(fd, cmd, arg);
        }

        // 1. 记录用户的意图
        // `arg & O_NONBLOCK` 检查用户是否想设置非阻塞模式
        fdCtx->setUserNonblock(arg & O_NONBLOCK);
        if(fdCtx->getSysNonblock()) {
            arg |= O_NONBLOCK; // 系统已经是非阻塞了，确保 arg 里也有 O_NONBLOCK 位
        } else {
            arg &= ~O_NONBLOCK; // 系统不是非阻塞的，确保 arg 里没有 O_NONBLOCK 位
        }
        return fcntl_f(fd, cmd, arg);
        } break;
    case F_GETFL:{
            va_end(va);
            int arg = fcntl_f(fd, cmd);
            FdCtx::FdCtxPtr fdCtx = FdManager::getInstance().get(fd);
            if(!fdCtx || fdCtx->isClosed() || fdCtx->getUserNonblock()) {
                return arg;
            }

            if(fdCtx->getUserNonblock()) {
                return arg | O_NONBLOCK; // 用户想要非阻塞，确保返回值里有 O_NONBLOCK 位
            } else {
                return arg & ~O_NONBLOCK; // 用户不想要非阻塞，确保返回值里没有 O_NONBLOCK 位
            }
        } break;
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
    case F_SETFD:
    case F_SETOWN:
    case F_SETSIG:
    case F_SETLEASE:
    case F_NOTIFY:
#ifdef F_SETPIPE_SZ
    case F_SETPIPE_SZ:
#endif
        {
        int arg = va_arg(va, int);
        va_end(va);
        return fcntl_f(fd, cmd, arg);
        } break;
    case F_GETFD:
    case F_GETOWN:
    case F_GETSIG:
    case F_GETLEASE:
#ifdef F_GETPIPE_SZ
    case F_GETPIPE_SZ:
#endif
        {
        va_end(va);
        return fcntl_f(fd, cmd);
        } break;
    case F_SETLK:
    case F_SETLKW:
    case F_GETLK: {
        struct flock *arg = va_arg(va, struct flock *);
        va_end(va);
        return fcntl_f(fd, cmd, arg);
        } break;
    case F_GETOWN_EX:
    case F_SETOWN_EX: {
        struct f_owner_exlock *arg = va_arg(va, struct f_owner_exlock *);
        va_end(va);
        return fcntl_f(fd, cmd, arg);
        } break;
    default:
        va_end(va);
        return fcntl_f(fd, cmd);
  }
}

// 这里只关心一个对 socket 设置非阻塞的命令 FIONBIO
int ioctl(int fd, unsigned long request, ...) {
    va_list va;
    va_start(va, request);
    void* arg = va_arg(va, void*);
    va_end(va);

    // 如果命令是 FIONBIO (File IO Non-Blocking)
    if(request == FIONBIO) {
        bool userFlag = *(int*)arg != 0; // 用户想要非阻塞还是阻塞
        FdCtx::FdCtxPtr fdCtx = FdManager::getInstance().get(fd);
        if(!fdCtx || fdCtx->isClosed() || !fdCtx->isSocket()) {
            return ioctl_f(fd, request, arg);
        }

        fdCtx->setUserNonblock(userFlag); // 记录用户的意图
    }

    // 总是调用原始函数（因为 ioctl 设了 FIONBIO 后，内核已经把 fd 变成非阻塞了）
    return ioctl_f(fd, request, arg);
}

int getsockopt(int sockfd, int level, int optname, void* optval, socklen_t* optlen) {
    return getsockopt_f(sockfd, level, optname, optval, optlen);
}

int setsockopt(int sockfd, int level, int optname, const void* optval, socklen_t optlen) {
    if(!isHookEnable()) {
        return setsockopt_f(sockfd, level, optname, optval, optlen);
    }

    // 只关心 socket 级别的选项
    if(level == SOL_SOCKET) {
        if(optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
            FdCtx::FdCtxPtr fdCtx = FdManager::getInstance().get(sockfd);
            if(fdCtx) {
                // 从 optval 中解析出 timeval 结构体
                const timeval* tv = (const timeval*)optval;
                // 转换成毫秒，并记录到 FdCtx 中
                fdCtx->setTimeout(optname, tv->tv_sec * 1000 + tv->tv_usec / 1000);
            }
        }
    }

    // 仍然调用原始函数，让内核也设置超时
    // (虽然对非阻塞 socket 没用，但保持行为一致)
    return setsockopt_f(sockfd, level, optname, optval, optlen);
}

}
}