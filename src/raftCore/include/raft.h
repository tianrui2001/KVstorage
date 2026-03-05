#pragma once

#include <mutex>
#include <vector>
#include <memory>
#include <google/protobuf/service.h>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/string.hpp>

#include "raftRPC.pb.h"
#include "raftRpcUtil.h"
#include "Persister.h"
#include "util.hpp"
#include "ApplyMsg.h"
#include "chrono"
#include "monsoon.hpp"

// 用于调试的，网络异常的时候为 disconnected， 网络正常的时候是AppNormal，防止matchIndex[]数组异常减小
constexpr int Disconnected = 0;
constexpr int AppNormal = 1;

enum VoteState {
    Killed = 0,
    Voted,      //本轮已经投过票了
    Expire,     //投票（消息、竞选者）过期
    Normal
};


class Raft :public raftRpcProctoc::raftRpc {
public:
    void init(std::vector<std::shared_ptr<RaftRPCUtil>> peers,  int raftId,
        std::shared_ptr<Persister> persister, 
        std::shared_ptr<LockQueue<ApplyMsg>> applyChan);

    void electionTimeOutTicker();
    void leaderHeartBeatTicker();
    void doHeartBeat();
    void doElection();

    void start(Op command, int *newLogIndex, int *newLogTerm, bool *isLeader);

    void leaderUpdateCommitIndex();

    void appendEntriesimpl(const raftRpcProctoc::AppendEntriesArgs* args, 
                            raftRpcProctoc::AppendEntriesReply* reply);
    void requestVoteImpl(const raftRpcProctoc::RequestVoteArgs* args, 
                            raftRpcProctoc::RequestVoteReply* reply);
    void installSnapshotImpl(const raftRpcProctoc::InstallSnapshotRequest* args, 
                            raftRpcProctoc::InstallSnapshotResponse* reply);

    // ========== 快照 ========== 

    // 这个函数是用在当leader发现某个follower的日志落后太多了，直接发送快照而不是日志了
    void leaderSendSnapshot(int peerId);
    bool condInstallSnapshot(int lastIncludedTerm, int lastIncludedIndex, std::string snapshot);
    void snapshot(int index, std::string snapshot);

    // ========== 读写持久化 ========== 

    std::string persistData();
    void readPersist(std::string data);
    void persist();

    // ========== 日志下标有关的操作 ========== 

    void getPrevLogInfo(int raftId, int *preIndex, int *preTerm);
    int getSlicesIndexFromLogIndex(int logIndex);
    int getLastLogIndex();
    int getLastLogTerm();
    void getLastLogIndexAndTerm(int *lastLogIndex, int *lastLogTerm);
    bool matchLog(int logIndex, int logTerm);
    int getLogTermFromLogIndex(int logIndex);

    bool upToDate(int index, int term);
    int getNewCommandIndex();
    int getRaftStateSize();

    // ========== applyMsg 相关的操作 ==========
    void applierTicker();
    std::vector<ApplyMsg> getApplyLogs();
    void getState(int *term, bool *isLeader);
    void pushMsgToKvServer(ApplyMsg msg);

public:
    // 重写基类方法：

    void AppendEntries(google::protobuf::RpcController* controller,
                       const ::raftRpcProctoc::AppendEntriesArgs* request,
                       ::raftRpcProctoc::AppendEntriesReply* response,
                       ::google::protobuf::Closure* done);
    void InstallSnapshot(google::protobuf::RpcController* controller,
                       const ::raftRpcProctoc::InstallSnapshotRequest* request,
                       ::raftRpcProctoc::InstallSnapshotResponse* response,
                       ::google::protobuf::Closure* done);
    void RequestVote(google::protobuf::RpcController* controller,
                       const ::raftRpcProctoc::RequestVoteArgs* request,
                       ::raftRpcProctoc::RequestVoteReply* response,
                       ::google::protobuf::Closure* done);

    // 发送消息

    bool sendAppendEntries(int peerId, std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> args,
                            std::shared_ptr<raftRpcProctoc::AppendEntriesReply> reply,
                            std::shared_ptr<int> appendNums);
    bool sendRequestVote(int peerId, std::shared_ptr<raftRpcProctoc::RequestVoteArgs> args,
                            std::shared_ptr<raftRpcProctoc::RequestVoteReply> reply,
                            std::shared_ptr<int> voteNums);

private:
    std::mutex mutex_;
    std::vector<std::shared_ptr<RaftRPCUtil>> peers_;   // 其他节点的RPC工具类实例
    std::shared_ptr<Persister> persister_;              // 持久化工具类实例

    int raftId_;    // 当前节点的ID
    int currentTerm_;    // 当前任期号
    int votedFor_;       // 本轮投票的候选者ID
    std::vector<raftRpcProctoc::LogEntry> logs_;   // 日志条目列表

    int commitIndex_;    // 已经提交的最高日志条目的索引
    int lastApplied_;    // 已经应用到状态机（上层应用）的最高日志条目的索引

    std::vector<int> nextIndex_;  // leader使用，发送给follower服务器的下一个日志条目的逻辑索引， 从1开始
    std::vector<int> matchIndex_; // leader使用，已经复制给follower服务器的最高日志条目的逻辑索引

    enum status {Follower, Candidate, Leader } status_;    // 当前节点的状态

    std::chrono::_V2::system_clock::time_point lastResetElectionTime_;   // 选举超时
    std::chrono::_V2::system_clock::time_point lastResetHearBeatTime_;   // 记录上一次真正触发心跳或日志同步的时间点, 用于leader

    int lastSnapshotIncludeIndex_;    // 最近一次快照的索引
    int lastSnapshotIncludeTerm_;     // 最近一次快照的任期号

    std::shared_ptr<LockQueue<ApplyMsg>> applyChan_;    // client从这里取日志，client与raft通信的接口
    std::unique_ptr<monsoon::IOManager> ioManager_;    // 协程

private:
    // 用于持久化
    class BoostPersistRaftNode {
        public:
        friend class boost::serialization::access;

        template<typename Archive>
        void serialize(Archive &ar, const unsigned int version) {
            ar &currentTerm;
            ar &votedFor;
            ar &lastSnapshotIncludeIndex;
            ar &lastSnapshotIncludeTerm;
            ar &logs;
        }

        int currentTerm;
        int votedFor;
        int lastSnapshotIncludeIndex;
        int lastSnapshotIncludeTerm;
        std::vector<std::string> logs;
        std::unordered_map<std::string, int> umap;
    };
};