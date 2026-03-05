#pragma once

#include <vector>
#include <memory>

#include "raftServerRpcUtil.h"
#include "util.hpp"
#include "mprpcconfig.h"

// 客户端代理类。内部维护了一个服务器列表，每个实例对应一个远程的 KV 服务器。
// 客户端调用该类的 Get、Put 和 Append 方法，
// 实质上是通过 RPC 将请求发送给远程的 KV 服务器执行，并返回结果。
class Clerk {
public:
    Clerk();

    void init(std::string configFileName);

    // Get、Put 和 Append 方法
    std::string Get(std::string key);
    void Put(std::string key, std::string value);
    void Append(std::string key, std::string value);

    std::string Uuid();
    void putAppend(std::string key, std::string value, std::string op);

private:
    std::vector<std::shared_ptr<raftServerRpcUtil>> servers_;   // 连接到所有服务器的RPC工具类实例列表
    std::string clientId_;  // 每个对象随机生成一个唯一的clientId，用于服务器端识别不同的客户端
    int requestId_;
    int recentLeaderId_;   // 最近一次成功请求的leader节点id，初始值为0
};