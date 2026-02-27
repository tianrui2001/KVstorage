#include <iostream>

#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "rpcprovider.h"

#include <rpcExample/friend.pb.h>

int main(int argc, char** argv) {
    std::string ip = "127.0.1.1";
    uint16_t port = 7788;

    // 演示调用远程发布的rpc方法Login
    fixbug::FriendServiceRpc_Stub stub(new MprpcChannel(ip, port, true));

    // rpc方法的请求参数
    fixbug::GetFriendsListRequest req;
    req.set_userid(1000);

    // rpc方法的响应
    fixbug::GetFriendsListResponse resp;
    MprpcController ctrl;

    // 长连接测试
    int count = 10;
    while(count--) {
        std::cout << "倒数第" << count << "次发起 RPC 调用" << std::endl;

        // 最终调用到 RpcChannel->RpcChannel::callMethod ，集中来做所有rpc方法调用的参数序列化和网络发送
        stub.GetFriendsList(&ctrl, &req, &resp, nullptr);

        // 一次rpc调用完成，读调用的结果
        // rpc调用是否失败由框架来决定（rpc调用失败 ！= 业务逻辑返回false）
        // rpc和业务本质上是隔离的
        if(ctrl.Failed()) {
            std::cout << ctrl.ErrorText() << std::endl;
        } else {
            if(resp.result().errcode() == 0) {
                std::cout << "rpc GetFriendsList response success! friends list:" << std::endl;
                for(int i = 0; i < resp.friends_size(); ++i) {
                    std::cout << "indx:" << i << " friend name: " << resp.friends(i) << std::endl;
                }
            } else {
                std::cout << "rpc GetFriendsList response error! errcode:" << resp.result().errcode() 
                          << " errmsg:" << resp.result().errmsg() << std::endl;
            }
        }

        sleep(5);
    }

    return 0;
}
