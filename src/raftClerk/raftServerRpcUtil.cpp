#include "raftServerRpcUtil.h"

bool raftServerRpcUtil::Get(raftKVRpcProctoc::GetArgs *args, raftKVRpcProctoc::GetReply *reply) {
    MprpcController controller;
    stub_->Get(&controller, args, reply, nullptr);
    return !controller.Failed();
}

bool raftServerRpcUtil::PutAppend(raftKVRpcProctoc::PutAppendArgs *args, 
                                raftKVRpcProctoc::PutAppendReply *reply)
{
    MprpcController controller;
    stub_->PutAppend(&controller, args, reply, nullptr);
    if (controller.Failed()) {
        std::cout << controller.ErrorText() << std::endl;
    }
    return !controller.Failed();
}

raftServerRpcUtil::raftServerRpcUtil(std::string ip, short port) {
    stub_ = new raftKVRpcProctoc::kvServerRpc_Stub(
        new MprpcChannel(ip, port, false)
    );
}

raftServerRpcUtil::~raftServerRpcUtil() {
    delete stub_;
}