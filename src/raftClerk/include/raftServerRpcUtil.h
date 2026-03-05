#pragma once

#include "kvServerRpc.pb.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"


// RPC 客户端代理。客户端调用该类的 Get 和 PutAppend 方法，
// 实质上是通过 RPC 将请求发送给远程的 KV 服务器执行，并返回结果。
class raftServerRpcUtil {
public:
    bool Get(raftKVRpcProctoc::GetArgs *args, raftKVRpcProctoc::GetReply *reply);
    bool PutAppend(raftKVRpcProctoc::PutAppendArgs *args, raftKVRpcProctoc::PutAppendReply *reply);

    raftServerRpcUtil(std::string ip, short port);
    ~raftServerRpcUtil();

private:
    raftKVRpcProctoc::kvServerRpc_Stub* stub_;
};