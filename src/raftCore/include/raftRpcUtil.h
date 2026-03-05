#pragma once
#include <string>

#include "raftRPC.pb.h"


/// @brief 维护当前节点对其他某一个结点的所有rpc发送通信的功能
// 对于一个raft节点来说，对于任意其他的节点都要维护一个rpc连接，即MprpcChannel
class RaftRPCUtil {
public:
    
    bool AppendEntries(raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *reply);
    bool InstallSnapshot(raftRpcProctoc::InstallSnapshotRequest *req, raftRpcProctoc::InstallSnapshotResponse *resp);
    bool RequestVote(raftRpcProctoc::RequestVoteArgs *req, raftRpcProctoc::RequestVoteReply *reply);

    // ip:远端的ip， port：远端的端口
    RaftRPCUtil(std::string ip, uint16_t port);
    ~RaftRPCUtil();

private:
    raftRpcProctoc::raftRpc_Stub *stub_;
};