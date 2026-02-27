#include <iostream>
#include <string>
#include <vector>

#include "mprpcchannel.h"
#include "rpcprovider.h"

#include "rpcExample/friend.pb.h"

class FriendService : public fixbug::FriendServiceRpc {
public:
    std::vector<std::string> GetFriendsList(uint32_t user_id) {
        std::cout << "local do GetFriendsList service! userid:" << user_id << std::endl;
        std::vector<std::string> friends_list;
        friends_list.push_back("zhangsan");
        friends_list.push_back("lisi");
        friends_list.push_back("wangwu");
        return friends_list;
    }

    void GetFriendsList(google::protobuf::RpcController* controller,
                       const ::fixbug::GetFriendsListRequest* request,
                       ::fixbug::GetFriendsListResponse* response,
                       ::google::protobuf::Closure* done)
    {
        uint32_t user_id = request->userid();
        std::vector<std::string> friends_list = GetFriendsList(user_id);
        response->mutable_result()->set_errcode(0);
        response->mutable_result()->set_errmsg("");
        for (const auto& friend_name : friends_list) {
            std::string *p = response->add_friends();
            *p = friend_name;
        }

        
        /*
        服务端之前绑定的 RpcProvider::SendRpcResponse(conn, response) 被执行。
        SendRpcResponse 函数内部会：
            将 response 对象序列化成二进制字符串。
            通过 conn 这个 TCP 连接，将二进制字符串发回给客户端。
        */
       done->Run();
    }
};

int main(int argc, char** argv) {
    std::string ip = "127.0.0.1";
    uint16_t port = 7788;

    auto stub = new fixbug::FriendServiceRpc_Stub(new MprpcChannel(ip, port, false));

    // provider是一个rpc网络服务对象。把UserService对象发布到rpc节点上
    RpcProvider provider;
    provider.NotifyService(new FriendService());

    // 启动一个rpc服务发布节点   Run以后，进程进入阻塞状态，等待远程的rpc调用请求
    provider.Run(1, port);

    return 0;
}
