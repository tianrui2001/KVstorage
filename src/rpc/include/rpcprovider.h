#pragma once

#include "google/protobuf/service.h"
#include <google/protobuf/descriptor.h>

#include <memory>
#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpConnection.h>
#include <unordered_map>
#include <string>

// 框架专用的发布rpc服务到网络对象类
class RpcProvider {
public:

    // 提供给外部使用的，可以发布rpc方法的函数接口。参数使用父类指针，方便后续扩展
    void NotifyService(google::protobuf::Service* service);

    // 启动rpc服务节点，开始提供rpc远程调用服务
    void Run(int nodeIndex, short port);

    ~RpcProvider();

private:
    // socket连接回调
    void OnConnection(const muduo::net::TcpConnectionPtr& conn);

    // 已建立连接用户的读写事件回调
    void OnMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf, muduo::Timestamp recvTime);

    // Closure的回调操作，用于序列化rpc的响应和网络发送
    void SendRpcResponse(const muduo::net::TcpConnectionPtr& conn, google::protobuf::Message* response);


    std::shared_ptr<muduo::net::TcpServer> muduo_server_;
    muduo::net::EventLoop loop_;

    // 存储注册的rpc服务对象和方法。key表示服务名称，value表示服务对象和方法列表
    struct ServiceInfo {
        google::protobuf::Service* service_;                                                    // 保存的服务对象
        std::unordered_map<std::string, const google::protobuf::MethodDescriptor*> methodMap_;  // 保存的服务方法列表
    };

    // 存储注册成功的服务对象和其服务方法的所有信息
    std::unordered_map<std::string, ServiceInfo> serviceMap_;
};