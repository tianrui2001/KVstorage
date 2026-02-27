#pragma once

#include <string>
#include <unordered_map>

// 配置文件的内容大致为： rpcserverip   rpcserverport    zookeeperip   zookeeperport
// 框架用来读取配置文件的类
class MrpcConfig {
public:
    // 加载配置文件，解析配置项，并将其存储在configMap_中
    void LoadConfigFile(const char* configFile);

    // 根据键获取配置项的值
    std::string Load(const std::string& key);

private:
    void Trim(std::string& str);    // 去除字符串两端的空格

    std::unordered_map<std::string, std::string> configMap_;    // 存储配置项的键值对
};