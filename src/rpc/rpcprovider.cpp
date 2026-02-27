#include <netdb.h>
#include <cstring>
#include <arpa/inet.h>
#include <fstream>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/io/coded_stream.h>

#include "rpcprovider.h"
#include "rpcheader.pb.h"

/*
service_name =>  service描述
                        => service* 记录服务对象(方法列表)：
                             method_name  =>  method方法对象
json   protobuf
*/
void RpcProvider::NotifyService(google::protobuf::Service* service){
    // 获取了服务对象的描述信息
    const google::protobuf::ServiceDescriptor* pServiceDesc = service->GetDescriptor();
    // 获取服务的名字
    std::string serviceName = pServiceDesc->name();
    // 获取服务对象service的方法的数量
    int methodCnt = pServiceDesc->method_count();
    std::cout << "service name: " << serviceName << std::endl;

    ServiceInfo serviceInfo;
    serviceInfo.service_ = service;
    for(int i=0; i<methodCnt; i++){
        // 获取了服务对象service的第i个方法的描述信息
        const google::protobuf::MethodDescriptor* pMethodDesc = pServiceDesc->method(i);
        std::string methodName = pMethodDesc->name();
        serviceInfo.methodMap_.insert({methodName, pMethodDesc});
    }

    serviceMap_.insert({serviceName, serviceInfo});
}

// 负责网络模块的初始化，启动网络服务，开始监听rpc调用请求
void RpcProvider::Run(int nodeIndex, short port){
    // 实现一个简单的服务发现机制。它把自己（作为 RPC 服务提供者）的地址信息广播出去，
    // 让客户端（Caller）知道该连哪里。这个只是一个简单的实现，工业级的实现是使用 Zookeeper、Etcd 这样的分布式协调服务。
    char *ip_ptr;
    char hostName[128];
    gethostname(hostName, sizeof(hostName));    // 1. 获取本机的主机名 (hostname)，比如 "my-machine"
    struct hostent* host = gethostbyname(hostName); // 2. 根据主机名查询 DNS，获取主机相关信息，包括 IP 地址列表（现在推荐用 getaddrinfo）

    // 3. 遍历 IP 地址列表
    // 一台机器可能有多个网络接口（比如有线网卡、无线网卡、Docker虚拟网卡），所以有多个 IP。
    // 这个循环会找到最后一个 IP 地址。
    for(int i=0; host->h_addr_list[i]; i++){
        ip_ptr = inet_ntoa(*(struct in_addr*)host->h_addr_list[i]);
    }

    // 4. 输出获取到的 IP 地址和端口号
    std::string ip = std::string(ip_ptr);
    
    //写入文件 "test.conf"
    std::string node = "node" + std::to_string(nodeIndex);
    std::ofstream outFile("test.conf", std::ios::app);  // 以追加模式打开文件
    if(!outFile.is_open()) {
        std::cerr << "Failed to open file for writing." << std::endl;
        exit(EXIT_FAILURE);
    }
    outFile << node << "ip=" + ip << std::endl;
    outFile << node << "port=" + std::to_string(port) << std::endl;
    outFile.close();
    // ----------------------------------------------------------------------

    // 启动一个 RPC 服务节点，提供 RPC 远程调用服务
    muduo::net::InetAddress serverAddr(ip, port);
    muduo_server_ = std::make_shared<muduo::net::TcpServer>(&loop_, serverAddr, "RpcProvider");

    muduo_server_->setConnectionCallback(
        std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1)
    );
    muduo_server_->setMessageCallback(
        std::bind(&RpcProvider::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
    );

    muduo_server_->setThreadNum(4);   // 设置底层sub Reactor的线程数量，默认为0
    muduo_server_->start();   // 启动服务
    loop_.loop();   // 启动事件循环
}


void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr& conn){
    if(!conn->connected()){
        conn->shutdown();   // 断开和rpc client的连接
    }
}


/*
在框架内部，RpcProvider和RpcConsumer协商好之间通信用的protobuf数据类型;
service_name method_name args    定义proto的message类型，进行数据头的序列化和反序列化
格式：                                 
header_size(4个字节) + header_str + args_str:

例如：16UserServiceLoginzhang san123456
header_size = 16; header_str = service_name + method_name + args_size;

header_size 整数使用二进制形式存储，而不是使用 std::to_string 转换成字符串形式存储。
因为整数的二进制形式占用固定的字节数（比如4个字节），而字符串形式的整数可能占用更多的字节数。
使用 std::string 的 insert 和 copy 方法
*/
// 如果远程有一个 rpc 服务的调用请求，那么 OnMessage 方法就会响应。执行序列化和反序列化
void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf, 
                            muduo::Timestamp recvTime){
    // 网络上接收的远程rpc调用请求的字符流    Login args
    std::string recvBuf = buf->retrieveAllAsString();
    
    // 使用protobuf的CodedInputStream来解析数据流
    google::protobuf::io::ArrayInputStream arrInput(recvBuf.data(), recvBuf.size());
    google::protobuf::io::CodedInputStream codedInput(&arrInput);
    uint32_t headerSize = 0;
    codedInput.ReadVarint32(&headerSize);  // 读取数据头的大小

    // 根据header_size读取数据头的原始字符流，反序列化数据，得到rpc请求的详细信息
    std::string headerStr;
    rpc::RpcHeader rpcHeader;
    std::string serviceName;
    std::string methodName;
    uint32_t argsSize{0};
    
    // 设置读取限制，不必担心数据读多
    google::protobuf::io::CodedInputStream::Limit limit = codedInput.PushLimit(headerSize);
    codedInput.ReadString(&headerStr, headerSize);  // 读取数据头的
    codedInput.PopLimit(limit);  // 恢复之前的读取限制
    if(rpcHeader.ParseFromString(headerStr)){
        serviceName = rpcHeader.service_name();
        methodName = rpcHeader.method_name();
        argsSize = rpcHeader.args_size();
    } else {
        std::cerr << "parse rpc header error!" << std::endl;
        return;
    }

    // 获取rpc方法参数的字符流数据
    std::string argsStr;
    if(!codedInput.ReadString(&argsStr, argsSize)) {
        std::cerr << "parse rpc args error!" << std::endl;
        return;
    }

    // 获取service对象和method对象
    auto it = serviceMap_.find(serviceName);
    if(it == serviceMap_.end()) {
        std::cout << "服务：" << serviceName << " is not exist!" << std::endl;
        std::cout << "当前已经有的服务列表为:";
        for (auto item : serviceMap_) {
        std::cout << item.first << " ";
        }
        std::cout << std::endl;
        return;
    }

    auto mit = it->second.methodMap_.find(methodName);
    if(mit == it->second.methodMap_.end()) {
        std::cout << serviceName << ":" << methodName << " is not exist!" << std::endl;
        return;
    }

    google::protobuf::Service* service = it->second.service_;
    const google::protobuf::MethodDescriptor* method = mit->second;

    // 生成rpc方法调用的请求request和响应response参数,由于是rpc的请求，因此请求需要通过request来序列化。
    // req 的参数在调用端已经定义好了
    google::protobuf::Message* req = service->GetRequestPrototype(method).New();    // 动态地创建了一个 Request 对象
    if(!req->ParseFromString(argsStr)){
        std::cerr << "parse rpc args error, req is invalid!" << std::endl;
        return;
    }
    google::protobuf::Message* resp = service->GetResponsePrototype(method).New();


    // 给service->CallMethod()方法绑定一个回调函数。当service->CallMethod()方法执行完成后，回调函数会被调用，负责序列化rpc的响应和网络发送
    // 使用模板函数 NewCallback， <>中间的参数是函数参数的类型列表，括号中的参数是函数参数的值列表
    google::protobuf::Closure* done = 
        google::protobuf::NewCallback<RpcProvider, const muduo::net::TcpConnectionPtr&, google::protobuf::Message*>(
            this, &RpcProvider::SendRpcResponse, conn, resp
        );
    
    // 在框架上根据远端rpc请求，调用当前rpc节点上发布的方法，比如说 GetFriendsList(, , , )
    service->CallMethod(method, nullptr, req, resp, done);
}

void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr& conn, google::protobuf::Message* response){
    std::string respStr;    // 用于存储序列化后的响应字符串
    if(response->SerializeToString(&respStr)){
        conn->send(respStr);
    } else {
        std::cerr << "serialize response error!" << std::endl;
    }
}

RpcProvider::~RpcProvider() {
    std::cout << "[func - RpcProvider::~RpcProvider()]: ip和port信息：" << muduo_server_->ipPort() << std::endl;
    loop_.quit();
}

