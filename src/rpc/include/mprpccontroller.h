#pragma once

#include <string>
#include <google/protobuf/service.h>

// 帮助我们携带一些rpc调用过程中的一些状态信息
class MprpcController : public google::protobuf::RpcController {
public:
    MprpcController();

    // 重写父类的方法
    void Reset();
    bool Failed() const;
    std::string ErrorText() const;
    void SetFailed(const std::string& reason);

    // 暂时还没有实现
    void StartCancel();
    bool IsCanceled() const;
    void NotifyOnCancel(google::protobuf::Closure* callback);
    
private:
    bool failed_ = false;    // rpc调用是否失败
    std::string errorMsg_;    // rpc调用失败的错误信息
};