#include <iostream>
#include <unistd.h>
#include <random>

#include "raft.h"
#include "KvServer.h"

// 本文件要使用./raftCoreRun -n 3 -f test.conf命令才能运行

void showArgs() {
    std::cerr << "format: command -n <nodeNum> -f <configFileName>" << std::endl;
}


int main(int argc, char** argv) {
    if(argc != 5) {
        showArgs();
        exit(EXIT_FAILURE);
    }

    // 获取命令行参数
    int c = 0;
    int nodeNum = 0;
    std::string configFileName;
    while((c = getopt(argc, argv, "n:f:")) != -1) { // n:f:表示n和f后面都要跟参数
        switch (c)
        {
        case 'n':
            nodeNum = std::atoi(optarg);
            break;
        case 'f':
            configFileName = optarg;
            break;
        
        default:
            showArgs();
            exit(EXIT_FAILURE);
        }
    }

    // 打开配置文件
    std::ofstream file(configFileName, std::ios::out | std::ios::trunc);
    if(!file.is_open()) {
        std::cerr << "无法打开配置文件： " << configFileName << std::endl;
        exit(EXIT_FAILURE);
    } else {
        file.close();
        std::cout << "已清空配置文件: " << configFileName << std::endl;
    }
    
    // 生成随机的端口号
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(10000, 29999);
    unsigned short startPort = dis(gen);

    for(int i = 0; i < nodeNum; i++) {
        short port = startPort + static_cast<short>(i);
        std::cout << "开始创建 raftKV node" << i << "， port: " << port << " pid: " << getpid() << std::endl;
        pid_t pid = fork();  // 创建子进程
        if(pid == 0) {
            auto kvServer = new KvServer(i, 500, configFileName, port);
            pause(); // 暂停子进程，等待外部信号来继续执行
        } else if(pid > 0) {
            sleep(1); // 父进程等待一段时间，确保子进程已经启动并写入了配置信息
        } else {
            std::cerr << "创建子进程失败！" << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    pause(); // 暂停父进程，等待外部信号来继续执行
    return 0;
}