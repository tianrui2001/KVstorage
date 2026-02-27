#pragma once

#include <string>
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

// 真正负责发送和接受的前后处理工作。 
// channel类用于自定义发送格式和负责序列化等操作
//  如消息的组织方式，向哪个节点发送等等
class MprpcChannel : public google::protobuf::RpcChannel {
public:
    MprpcChannel(std::string ip, uint16_t port, bool connectNow);

    // 重写RpcChannel里面的CallMethod方法
    // 所有通过stub代理对象调用的rpc方法，都走到这里了，统一做rpc方法调用的数据数据序列化和网络发送那一步
    void CallMethod(const google::protobuf::MethodDescriptor* method,
                          google::protobuf::RpcController* controller, const google::protobuf::Message* request,
                          google::protobuf::Message* response, google::protobuf::Closure* done);

private:
    // 连接ip和端口,并设置 sockfd_
    bool newConnect(const char* ip, uint16_t port, std::string* errorMsg);

    int sockfd_ = -1;    // socket文件描述符
    const std::string ip_;  // 保存 IP 地址 和 端口号，如果断开可以重新连接
    const uint16_t port_;
};
