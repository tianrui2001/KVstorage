#include <random>
#include <sstream>
#include <iomanip>
#include <mutex>

#include "clerk.h"

Clerk::Clerk()
    : clientId_(Uuid()), requestId_(0), recentLeaderId_(0)
{}

void Clerk::init(std::string configFileName) {
    // 从配置文件中加载服务器地址，并创建对应的 RPC 工具类实例，存储在 servers_ 中
    MrpcConfig conf;
    conf.LoadConfigFile(configFileName.c_str());
    std::vector<std::pair<std::string, short>> serversAddrs;
    for(int i = 0; i < INT_MAX - 1; i++) {
        std::string node = "node" + std::to_string(i);

        std::string nodeIp = conf.Load(node + "ip");
        std::string nodePortStr = conf.Load(node + "port");
        if(nodeIp.empty()) {
            break; // 没有更多的节点了，退出循环
        }

        serversAddrs.emplace_back(nodeIp, static_cast<short>(std::stoi(nodePortStr)));
    }

    for(const auto& add : serversAddrs) {
        raftServerRpcUtil* rpcUtil = new raftServerRpcUtil(add.first, add.second);
        servers_.push_back(std::shared_ptr<raftServerRpcUtil>(rpcUtil));
    }
}

// 这个函数会一直尝试向服务器发送请求，直到成功为止。它会根据服务器的回复来判断是否需要换服务器重试。
std::string Clerk::Get(std::string key) {
    requestId_++;
    int leaderId = recentLeaderId_;

    raftKVRpcProctoc::GetArgs args;
    args.set_key(key);
    args.set_clientid(clientId_);
    args.set_requestid(requestId_);
    
    while(true) {
        raftKVRpcProctoc::GetReply reply;
        bool isOk = servers_[leaderId]->Get(&args, &reply);
        if(!isOk || reply.err() == ErrWrongLeader) {
            // 请求失败了，或者服务器回复说它不是leader了，那就换下一个服务器试试
            leaderId = (leaderId + 1) % servers_.size();
            continue;
        }

        if(reply.err() == ErrNoKey) {
            return "";  // 服务器回复说没有这个key了，那就直接返回空字符串，不需要继续尝试了
        }

        if(reply.err() == OK) {
            recentLeaderId_ = leaderId;
            return reply.value();
        }
    }

    return "";
}

void Clerk::Put(std::string key, std::string value) {
    putAppend(key, value, "Put");
}

void Clerk::Append(std::string key, std::string value) {
    putAppend(key, value, "Append");
}

std::string Clerk::Uuid() {
    // 初始化 mt19937 太慢了，且频繁初始化会破坏随机分布特性,所以我们使用静态变量来保证只初始化一次
    static std::mutex uuidMutex;
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis(0, 0xFFFFFFFFFFFFFFFF);

    std::lock_guard<std::mutex> lock(uuidMutex);
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << dis(gen);
    ss << std::setw(16) << std::setfill('0') << dis(gen);
    return ss.str();
}

void Clerk::putAppend(std::string key, std::string value, std::string op) {
    requestId_++;
    int leaderId = recentLeaderId_;

    raftKVRpcProctoc::PutAppendArgs args;
    args.set_key(key);
    args.set_value(value);
    args.set_op(op);
    args.set_clientid(clientId_);
    args.set_requestid(requestId_);

    while(true) {
        raftKVRpcProctoc::PutAppendReply reply;
        bool isOk = servers_[leaderId]->PutAppend(&args, &reply);
        if(!isOk || reply.err() == ErrWrongLeader) {
            leaderId = (leaderId + 1) % servers_.size();
            continue;
        }

        if(reply.err() == OK) {
            recentLeaderId_ = leaderId;
            return; // 成功了，直接返回
        }
    }
}