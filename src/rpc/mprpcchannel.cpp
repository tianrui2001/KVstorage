#include <iostream>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/io/coded_stream.h>


#include "mprpcchannel.h"
#include "rpcheader.pb.h"
#include "util.hpp"

/*
header_size + service_name method_name args_size + args
*/
// 所有通过stub代理对象调用的rpc方法，都会走到这里了，
// 统一通过rpcChannel来调用方法
// 统一做rpc方法调用的数据数据序列化和网络发送
void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                                google::protobuf::RpcController* controller, 
                                const google::protobuf::Message* request,
                                google::protobuf::Message* response, 
                                google::protobuf::Closure* done)
{
    if(sockfd_ < 0) {
        std::string errorMsg;
        if(!newConnect(ip_.c_str(), port_, &errorMsg)){
            DPrintf("[func-MprpcChannel::CallMethod]重连接ip：{%s} port{%d}失败", ip_.c_str(), port_);
            controller->SetFailed(errorMsg);
            return;
        } else {
            DPrintf("[func-MprpcChannel::CallMethod]连接ip：{%s} port{%d}成功", ip_.c_str(), port_);
        }
    }

    const google::protobuf::ServiceDescriptor* service = method->service();
    std::string service_name = service->name();
    std::string method_name = method->name();

    // 获取参数的序列化字符串长度 args_size
    uint32_t args_size = 0;
    std::string args_str;
    if(request->SerializeToString(&args_str)) {
        args_size = args_str.size();
    } else {
        controller->SetFailed("serialize request error!");
        return;
    }

    // 定义rpc请求的header
    rpc::RpcHeader rpcHeader;
    rpcHeader.set_service_name(service_name);
    rpcHeader.set_method_name(method_name);
    rpcHeader.set_args_size(args_size);

    // 将rpc请求header进行序列化
    std::string rpc_header_str;
    if(!rpcHeader.SerializeToString(&rpc_header_str)){
        controller->SetFailed("serialize rpc header error!");
        return;
    }

    // 使用protobuf的CodedOutputStream来构建发送的数据流
    std::string send_rpc_str;
    {
        // 创建一个StringOutputStream用于写入send_rpc_str
        google::protobuf::io::StringOutputStream output(&send_rpc_str);
        google::protobuf::io::CodedOutputStream codeOutput(&output);

        // 先写入header的长度（变长编码）
        codeOutput.WriteVarint32(rpc_header_str.size());

        // 然后写入rpc_header本身
        codeOutput.WriteString(rpc_header_str);
    }

    // 最后，将请求参数附加到send_rpc_str后面
    send_rpc_str += args_str;

    // 发送rpc请求
    //失败会重试连接再发送，重试连接失败会直接return
    while(send(sockfd_, send_rpc_str.c_str(), send_rpc_str.size(), 0) < 0) {
        DPrintf("[func-MprpcChannel::CallMethod]send error: %s, try to reconnect...", strerror(errno));
        close(sockfd_);
        sockfd_ = -1;
        std::string errorMsg;
        if(!newConnect(ip_.c_str(), port_, &errorMsg)) {
            DPrintf("[func-MprpcChannel::CallMethod]connect error: %s, retrying...", errorMsg.c_str());
            controller->SetFailed(errorMsg);
            return;
        }
    }

    // 将请求发送出去之后服务提供者就会开始处理， 当处理完成，返回响应信息

    // 接收rpc响应信息
    char recv_buf[1024] = {0};
    int recv_size = 0;
    recv_size = recv(sockfd_, recv_buf, sizeof(recv_buf), 0);   // 不要忘了赋值 recv_size ，否则可能会出错
    if( recv_size < 0) {
        close(sockfd_);
        sockfd_ = -1;
        DPrintf("[func-MprpcChannel::CallMethod]recv error: %s", strerror(errno));
        controller->SetFailed("recv error: " + std::string(strerror(errno)));
        return;
    }

    // 反序列化rpc响应信息到response对象
    // 不要使用std::string response_str(recv_buf, 0, recv_size);
    // 因为recv_buf中遇到\0后面的数据就存不下来了，导致反序列化失败
    if(!response->ParseFromArray(recv_buf, recv_size)) {
        DPrintf("[func-MprpcChannel::CallMethod]parse response error");
        controller->SetFailed("parse response error");
        return;
    }

    // 不应该在这里关闭fd，因为可能会有多次调用，保持长连接
}

bool MprpcChannel::newConnect(const char* ip, uint16_t port, std::string* errorMsg) {
    sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd_ < 0){
        sockfd_ = -1;
        *errorMsg = "create socket error: " + std::string(strerror(errno));
        return false;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    if(connect(sockfd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        *errorMsg = "connect error: " + std::string(strerror(errno));
        close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    return true;
}

MprpcChannel::MprpcChannel(std::string ip, uint16_t port, bool connectNow)
    : ip_(std::move(ip)), port_(port), sockfd_(-1)
{
    // 对于调用其他节点的 分布式节点，先初始化为false，等到其他节点启动了并且调用 CallMethod 时才进行TCP连接
    if(!connectNow) { 
        return;
    }

    // 如果失败，尝试 3 次连接服务器
    std::string errorMsg;
    int try_cnt = 3;
    bool ret = newConnect(ip_.c_str(), port_, &errorMsg);
    while(!ret && try_cnt--) {
        std::cerr << "connect error: " << errorMsg << ", retrying..." << std::endl;
        ret = newConnect(ip_.c_str(), port_, &errorMsg);
    }
}