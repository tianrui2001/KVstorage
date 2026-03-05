#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "monsoon.hpp"

const std::string LOG_HEAD = "[TASK] ";

void test_sleep() {
    monsoon::IOManager iom(1, true) ;

    iom.scheduler([](){
        while(true) {
            sleep(6);
            std::cout << "task 1 sleep for 6s" << std::endl;
        }
    });

    iom.scheduler([](){
        while(true) {
            sleep(2);
            std::cout << "task 2 sleep for 2s" << std::endl;
        }
    });

    std::cout << LOG_HEAD << "tid = " << monsoon::GetThreadId() << ",test_fiber_sleep finish" << std::endl;
}

void test_socket() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "36.152.44.96", &addr.sin_addr.s_addr);

    std::cout << "begin connect " << std::endl;
    int ret = connect(sockfd, (const sockaddr *)&addr, sizeof(addr));
    std::cout << "connect ret = " << ret << ", errno = " << errno << std::endl;
    if(ret) {
        return;
    }

    // 没有向服务器发送请求，
    const char* request = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n";
    send(sockfd, request, strlen(request), 0);
    std::cout << "send request success, waiting for recv..." << std::endl;

    std::string buf;
    buf.resize(4096);
    ret = recv(sockfd, &buf[0], buf.size(), 0);
    std::cout << "recv ret = " << ret << ", errno = " << errno << std::endl;
    if(ret <= 0) {
        return;
    }

    buf.resize(ret);
    std::cout << "--------------------------------" << std::endl;
    std::cout << buf << std::endl;
    std::cout << "--------------------------------" << std::endl;
}

int main() {

    test_socket();
    test_sleep();
}