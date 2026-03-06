#include <iostream>
#include <fstream>
#include <cstdio>   // 用于 rename 函数

#include "Persister.h"

Persister::Persister(int raftId)
: raftStateFileName_("raftstate" + std::to_string(raftId) + ".txt"),
    raftSnapshotFileName_("raftsnap" + std::to_string(raftId) + ".txt"),
    raftStateSize_(0)
{
    // 启动时不要清空文件！尝试读取现有文件大小
    std::ifstream ifs(raftStateFileName_, std::ios::binary | std::ios::ate);
    if(ifs.is_open()){
        raftStateSize_ = ifs.tellg();
    } else {
        // 如果文件不存在，创建一个空文件
        std::ofstream ofs(raftStateFileName_, std::ios::binary);
        raftStateSize_ = 0;
    }
}

Persister::~Persister(){}

void Persister::Save(const std::string& state, const std::string& snapShot){
    std::lock_guard<std::mutex> lock(mutex_);
    atomicWrite(raftStateFileName_, state);
    atomicWrite(raftSnapshotFileName_, snapShot);
    raftStateSize_ = state.size();
}

std::string Persister::ReadSnapshot(){
    std::lock_guard<std::mutex> lock(mutex_);
    return ReadRawData(raftSnapshotFileName_);
}

void Persister::SaveRaftState(const std::string& state){
    std::lock_guard<std::mutex> lock(mutex_);
    atomicWrite(raftStateFileName_, state);
    raftStateSize_ = state.size();
}

long long Persister::RaftStateSize(){
    std::lock_guard<std::mutex> lock(mutex_);
    return raftStateSize_;
}

std::string Persister::ReadRaftState(){
    std::lock_guard<std::mutex> lock(mutex_);
    return ReadRawData(raftStateFileName_);
}


// 读取整个二进制文件
std::string Persister::ReadRawData(const std::string& fileName){
    std::ifstream ifs(fileName, std::ios::binary | std::ios::ate);
    if(!ifs.is_open()){
        return "";
    }

    std::streamsize size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    std::string buf;
    buf.resize(size);
    if(ifs.read(&buf[0], size)){
        return buf;
    } else {
        return "";
    }
}

// 原子性写入逻辑：写到临时文件 -> rename 覆盖原文件
void Persister::atomicWrite(const std::string& fileName, const std::string& data){
    std::string tempFile = fileName + ".temp";

    // 写入临时文件
    std::ofstream ofs(tempFile, std::ios::binary | std::ios::trunc | std::ios::out);
    if(!ofs.is_open()){
        return;
    }

    ofs.write(data.data(), data.size());
    ofs.flush();
    ofs.close();

    // 2. 调用系统原子操作 rename 覆盖原文件
    // 这保证了磁盘上永远有一份完整的（旧的或新的）数据
    if(std::rename(tempFile.c_str(), fileName.c_str()) != 0) {
        std::perror("Persister: rename failed");
    }
}