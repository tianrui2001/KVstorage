#include <mprpcchannel.h>
#include <mprpccontroller.h>

#include "raftRpcUtil.h"

// 调用 stub_ 的方法来发送rpc请求，得到rpc回复
bool RaftRPCUtil::AppendEntries(raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *reply){
    MprpcController contoller;
    stub_->AppendEntries(&contoller, args, reply, nullptr);
    return !contoller.Failed();
}

bool RaftRPCUtil::InstallSnapshot(raftRpcProctoc::InstallSnapshotRequest *req, raftRpcProctoc::InstallSnapshotResponse *resp){
    MprpcController controller;
    stub_->InstallSnapshot(&controller, req, resp, nullptr);
    return !controller.Failed();
}

bool RaftRPCUtil::RequestVote(raftRpcProctoc::RequestVoteArgs *req, raftRpcProctoc::RequestVoteReply *reply){
    MprpcController controller;
    stub_->RequestVote(&controller, req, reply, nullptr);
    return !controller.Failed();
}

// ip:远端的ip， port：远端的端口
RaftRPCUtil::RaftRPCUtil(std::string ip, uint16_t port){
    // 连接远端的ip和端口，创建stub对象，stub对象里面会创建MprpcChannel对象来连接远端
    stub_ = new raftRpcProctoc::raftRpc_Stub(new MprpcChannel(ip, port, true));
}

RaftRPCUtil::~RaftRPCUtil(){
    delete stub_;
}