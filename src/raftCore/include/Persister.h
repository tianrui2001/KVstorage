#pragma once

#include <mutex>
#include <string>

// 这部分如果按照企业级的写法完整写出来差不多是一个小型的数据库存储引擎了，
// 所以选择简化处理。这里我对原作者的代码进行了修改，感觉他的逻辑有点问题
class Persister {
public:
    explicit Persister(int raftId);
    ~Persister();

    // 原子性保存 Raft 状态和快照
    void Save(const std::string& state, const std::string& snapShot);

    // 读取快照
    std::string ReadSnapshot();

    // 原子性保存 Raft 状态
    void SaveRaftState(const std::string& state);

    // 获取当前 Raft 状态的大小
    long long RaftStateSize();

    // 读取 Raft 状态
    std::string ReadRaftState();


private:
    // 内部辅助函数：读取整个文件内容
    std::string ReadRawData(const std::string& fileName);

    // 内部辅助函数：原子性覆写文件
    void atomicWrite(const std::string& fileName, const std::string& data);

    std::mutex mutex_;
    const std::string raftStateFileName_;   // raftState: currentTerm, votedFor, logs序列化后的字节流
    const std::string raftSnapshotFileName_;
    long long raftStateSize_;
};