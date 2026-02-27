#include "mprpcconfig.h"

#include <iostream>

void MrpcConfig::LoadConfigFile(const char* configFile){
    FILE* fp = fopen(configFile, "r");
    if(fp == nullptr) {
        std::cerr << "Failed to open config file: " << configFile << std::endl;
        return;
    }

    while(!feof(fp)){
        char buf[512];
        if(fgets(buf, sizeof(buf), fp) == nullptr) {
            break;  // 读取失败或到达文件末尾，退出循环
        }

        std::string str(buf);
        Trim(str);  // 去除字符串两端的空格

        if(str.empty() || str[0] == '#') {
            continue;  // 跳过空行和注释行
        }

        int idx = str.find('=');
        if(idx == -1) {
            continue;  // 没有找到等号，跳过该行
        }

        std::string key = str.substr(0, idx);
        int endidx = str.find('\n');
        std::string value = str.substr(idx + 1, endidx -idx - 1);
        Trim(key);
        Trim(value);
        configMap_.insert({key, value});  // 将键值对插入到configMap_中
    }

    fclose(fp);
}

std::string MrpcConfig::Load(const std::string& key) {
    auto it = configMap_.find(key);
    if(it != configMap_.end()) {
        return it->second;
    }

    return "";  // 如果没有找到对应的键，返回空字符串
}

void MrpcConfig::Trim(std::string& str) {
    size_t start = str.find_first_not_of(" ");
    if(start != -1) {
        str = str.substr(start, str.size() - start);
    }

    size_t end = str.find_last_not_of(" ");
    if(end != -1) {
        str = str.substr(0, end + 1);
    }
}