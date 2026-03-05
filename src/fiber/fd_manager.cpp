#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>

#include "fd_manager.hpp"
#include "hook.hpp"

namespace monsoon {
FdCtx::FdCtx(int fd)
    : isInit_(false),
      isSocket_(false),
      sysNonblock_(false),
      userNonblock_(false),
      isClosed_(false),
      fd_(fd),
      recvTimeout_(0),
      sendTimeout_(0)
{
    init(); // 初始化上下文
}

FdCtx::~FdCtx() {}

void FdCtx::setTimeout(int type, uint64_t times){
    if(type == SO_RCVTIMEO){
        recvTimeout_ = times;
    } else {
        sendTimeout_ = times;
    }
}

uint64_t FdCtx::getTimeout(int type) const{
    if(type == SO_RCVTIMEO) {
        return recvTimeout_;
    } else {
        return sendTimeout_;
    }

}

bool FdCtx::init(){
    if(isInit_) {
        return true; // 已经初始化过了
    } 

    struct stat st;
    if(fstat(fd_, &st) == 0){
        isInit_ = true;
        isSocket_ = S_ISSOCK(st.st_mode); // 判断是否是 socket
    }

    if(isSocket_) {
        // fcntl_f 是 hook_init() 中指向 glibc 中原始 fcntl 函数的指针
        int flags = fcntl_f(fd_, F_GETFL, 0);
        if(!(flags & O_NONBLOCK)) {
            fcntl_f(fd_, F_SETFL, flags | O_NONBLOCK); // 设置为非阻塞
        }
        sysNonblock_ = true; // 系统非阻塞
    }

    return isInit_;
}

FdManager::FdManager() { fdCtxs_.resize(64); } // 初始容量为 64

FdManager& FdManager::getInstance() {
    static FdManager instance;
    return instance;
}

FdCtx::FdCtxPtr FdManager::get(int fd, bool autoCreate) {
    if(fd < 0) {
        return nullptr; // 无效的文件描述符
    }

    RWMutex::ReadLock lock(mutex_);
    if(fd > static_cast<int>(fdCtxs_.size())){
        if(!autoCreate) {
            return nullptr;
        }
    } else {
        if(fdCtxs_[fd] || !autoCreate) {
            return fdCtxs_[fd]; // 已经存在上下文，直接返回
        }
    }
    lock.unlock();

    RWMutex::WriteLock wrLock(mutex_);
    FdCtx::FdCtxPtr ctx(new FdCtx(fd));
    if(fd >= static_cast<int>(fdCtxs_.size())) {
        fdCtxs_.resize(fd * 1.5);
    }
    fdCtxs_[fd] = ctx;
    return ctx;
}

void FdManager::del(int fd) {
    RWMutex::WriteLock wrLock(mutex_);
    if(fd < static_cast<int>(fdCtxs_.size())) {
        fdCtxs_[fd].reset(); // 删除上下文
    }
}

}