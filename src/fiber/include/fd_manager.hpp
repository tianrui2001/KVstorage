#pragma once

#include <vector>
#include <memory>

#include "mutex.hpp"

namespace monsoon {

class FdCtx : public std::enable_shared_from_this<FdCtx>
{
public:
    using FdCtxPtr = std::shared_ptr<FdCtx>;

    FdCtx(int fd);
    ~FdCtx();

    bool isInit() const { return isInit_; }
    bool isSocket() const { return isSocket_; }
    bool isClosed() const { return isClosed_; }
    void setUserNonblock(bool v) { userNonblock_ = v; }
    bool getUserNonblock() const { return userNonblock_; }
    void setSysNonblock(bool v) { sysNonblock_ = v; }
    bool getSysNonblock() const { return sysNonblock_; }

    void setTimeout(int type, uint64_t times);
    uint64_t getTimeout(int type) const;


private:
    bool init(); // 初始化上下文
    
    // 使用位域，将多个 bool 压缩到一个整数里。节省内存
    bool isInit_ : 1; // 是否初始化
    bool isSocket_ : 1; // 是否是 socket
    bool sysNonblock_ : 1; // 是否是非阻塞
    bool userNonblock_ : 1; // 用户设置的非阻塞
    bool isClosed_ : 1; // 是否关闭
    int fd_ ; // 文件描述符
    uint64_t recvTimeout_;  // 读超时
    uint64_t sendTimeout_;  // 写超时
};

// 文件句柄管理, 单例模式
class FdManager
{
public:
    static FdManager& getInstance();

    // 获取 fd 的上下文，如果 autoCreate 为 true，且不存在，则创建一个新的上下文
    FdCtx::FdCtxPtr get(int fd, bool autoCreate = false);

    void del(int fd); // 删除 fd 的上下文

private:
    FdManager();

    RWMutex mutex_; // 读写锁，保护下面的 fdCtxs_ 的访问
    std::vector<FdCtx::FdCtxPtr> fdCtxs_; // 文件描述符上下文的数组，索引是文件描述符
};

}