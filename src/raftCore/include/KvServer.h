#pragma once

#include <mutex>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/unordered_map.hpp>    // 序列化unordered_map需要这个头文件
#include <boost/serialization/string.hpp>         // 序列化string需要这个头文件
#include <boost/serialization/vector.hpp>         // 序列化vector需要这个头文件

#include "raft.h"
#include "kvServerRpc.pb.h"
#include "skipList.h"

// 这个类是基于 raft 算法构建的 KV 存储服务器。在这里扮演状态机的角色。 他的作用如下：
// 作为业务层：它直接面对客户端，接收客户端发来的 Put、Append、Get RPC 请求。
// 与 Raft 交互：它不直接修改数据，而是将客户端的请求封装成 Op（操作日志），提交给底层的 Raft 节点。
// 保持一致性：它等待 Raft 达成共识。一旦 Raft 确认该日志已提交，KvServer 就会从 applyChan 中读取该日志，并真正将其应用到本地的存储引擎（这里是 SkipList 或 unordered_map）中。
// 快照管理：当 Raft 日志过大时，它负责将当前的 KV 数据打包成快照，告诉 Raft 截断旧日志。
class KvServer : raftKVRpcProctoc::kvServerRpc {
public:
    KvServer() = delete;

    KvServer(int raftId, int maxRaftStateSize, std::string nodeInfoFileName, short port);

    void dprintfKVDB();

    void readSnapshotToInstall(std::string snapshot);

    void execAppendOpOnKVDB(Op op);

    void execGetOpOnKVDB(Op op, std::string* value, bool* isExist);

    void execPutOpOnKVDB(Op op);

    void getCommandFromRaft(ApplyMsg msg);

    void getSnapshotFromRaft(ApplyMsg msg);

    void ifNeedToSendSnapshotCommand(int raftLogIndex, int proportion);

    std::string makeSnapshot();

    bool sendMsgToWaitChan(const Op& op, int raftLogIndex);

    bool isRequestDuplicate(std::string clientId, int requestId);

    void readApplyCommandLoop();
    
    void getImpl(const raftKVRpcProctoc::GetArgs* args, raftKVRpcProctoc::GetReply* reply);
    void putAppendImpl(const raftKVRpcProctoc::PutAppendArgs* args, raftKVRpcProctoc::PutAppendReply* reply);

    void PutAppend(google::protobuf::RpcController* controller,
                    const ::raftKVRpcProctoc::PutAppendArgs* request,
                    ::raftKVRpcProctoc::PutAppendReply* response,
                    ::google::protobuf::Closure* done);

    void Get(google::protobuf::RpcController* controller,
                    const ::raftKVRpcProctoc::GetArgs* request,
                    ::raftKVRpcProctoc::GetReply* response,
                    ::google::protobuf::Closure* done);

private:
    std::mutex mutex_;
    int raftId_;
    std::shared_ptr<Raft> raftNode_;
    std::shared_ptr<LockQueue<ApplyMsg>> applyChan_;    // kvServer和raft节点的通信管道
    int maxRaftStateSize_;  // 状态日志的最大容量，超过这个容量就需要进行快照了

    SkipList<std::string, std::string> skipList_;   // 实际存储键值对的地方

    // 等待应用完成的通知映射表。
    // 在调用 start() 提交日志后，客户端会在这个映射表中等待对应日志被应用完成的通知。
    // 一旦 applyChan_ 中有新的日志被应用完成，KvServer 就会检查这个映射表，
    // 如果有对应的日志索引，就会通知等待的客户端。
    std::unordered_map<int, LockQueue<Op> *> waitApplyCh_;   

    int lastSnapShotRaftLogIndex_; // 上一次触发快照时的 Raft 日志索引

private:
    /// 快照序列化和反序列化相关的操作:

    // 序列化后的 KV 数据，在制作快照的时候，将内存跳表中的数据转换成这个字符串进行处理
    std::string serializedKVData_;

    std::unordered_map<std::string, int> lastRequestIdMap_; // 客户端最后一次请求 ID 的记录,保证幂等性

    friend class boost::serialization::access;

    // boost的这种写法是侵入式的写法，之前看到的2个实现是非侵入式的。但是在这里侵入式的是推荐的
    template <typename Archive>
    void serialize(Archive& ar, const unsigned int version) {
        ar& serializedKVData_;
        ar& lastRequestIdMap_;
    }

    std::string getSnapshotData() {
        serializedKVData_ = skipList_.dumpFile(); // 将跳表中的数据序列化成字符串
        std::stringstream ss;
        boost::archive::text_oarchive oa(ss);
        oa << *this; // 序列化当前对象
        serializedKVData_.clear(); // 序列化完成后清空，节省内存
        return ss.str();
    }

    void loadFromSnapshotData(std::string snapshotData) {
        std::stringstream ss(snapshotData);
        boost::archive::text_iarchive ia(ss);
        ia >> *this; // 反序列化到当前对象
        skipList_.loadFile(serializedKVData_); // 从反序列化得到的字符串中重新加载跳表数据
        serializedKVData_.clear(); // 加载完成后清空，节省内存
    }
};
