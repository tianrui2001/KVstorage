#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <stack>
#include "monsoon.hpp"

/**
 * 使用./test_server 启动测试程序
 * 然后新建一个终端，输入telnet 127.0.0.1 8080 回车
 * 然后在新起的终端上面输入任意内容回车，测试程序会将输入的内容原样返回
 * 
 * ctl + ] 可以退出 telnet
 */

// 本测试实现了一个回声服务器，将对端发送过来的信息原样输出到终端

static int listen_sock = -1;

void test_accept();

void watch_io_read() {
    monsoon::IOManager::getThis()->addEvent(listen_sock, monsoon::Event::READ, test_accept);
}

void test_accept() {
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t len = sizeof(addr);
    int fd = accept(listen_sock, (sockaddr*)&addr, &len);
    if(fd < 0) {
        std::cout << "fd = " << fd << ",accept error" << std::endl;
    } else {
        std::cout << "fd = " << fd << ",accept success" << std::endl;
        fcntl(fd, F_SETFL, O_NONBLOCK);
        monsoon::IOManager::getThis()->addEvent(fd, monsoon::Event::READ, [fd](){
            char buf[1024];
            while(true) {
                memset(buf, 0, sizeof(buf));
                int ret = recv(fd, buf, sizeof(buf), 0);
                if(ret > 0) {
                    std::cout << "client say: " << buf << std::endl;
                    ret = send(fd, buf, ret, 0);
                } 
                if(ret <= 0) {
                    if(errno == EAGAIN) continue;
                    close(fd);
                    break;
                }
            }
        });
    }

    monsoon::IOManager::getThis()->scheduler(watch_io_read);
}

void test_iomanager() {
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);

    int port = 8080;
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cout << "bind error" << std::endl;
        return;
    }

    if(listen(listen_sock, 1024) < 0) {
        std::cout << "listen error" << std::endl;
        return;
    } else {
        std::cout << "listen success on port " << port << std::endl;
    }

    fcntl(listen_sock, F_SETFL, O_NONBLOCK);

    monsoon::IOManager iom;
    iom.addEvent(listen_sock, monsoon::Event::READ, test_accept);
}

int main() {
    test_iomanager();
    return 0;
}