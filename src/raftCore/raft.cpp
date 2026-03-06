#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

#include "raft.h"
#include "config.hpp"

// raft 的初始化函数，初始化成员变量，并从磁盘恢复状态（如果之前运行过的话）。最后启动选举定时器。
void Raft::init(std::vector<std::shared_ptr<RaftRPCUtil>> peers,  int raftId,
    std::shared_ptr<Persister> persister, 
    std::shared_ptr<LockQueue<ApplyMsg>> applyChan)
{
    peers_ = peers;
    persister_ = persister;
    raftId_ = raftId;

    mutex_.lock();
    applyChan_ = applyChan;     // applyChan 是和上层 KVServer 沟通的唯一桥梁

    // -----------------内存状态重置-----------------
    currentTerm_ = 0;
    status_ = Follower;
    commitIndex_ = 0;
    lastApplied_ = 0;
    logs_.clear();  // 清空日志

    for(int i = 0; i < peers_.size(); i++){
        // 为每一个 peer 初始化索引追踪
        nextIndex_.push_back(0);
        matchIndex_.push_back(0);
    }

    votedFor_ = -1;   // -1表示本轮没有投票
    lastSnapshotIncludeIndex_ = 0;
    lastSnapshotIncludeTerm_ = 0;

    lastResetElectionTime_ = now();
    lastResetHearBeatTime_ = now();
    
    // --------------- 持久化恢复----------------------
    // 如果这个节点之前运行过并崩溃了，它会从磁盘读取 currentTerm, votedFor 和 logs
    readPersist(persister_->ReadRaftState());
    if(lastSnapshotIncludeIndex_ > 0) {
        lastApplied_ = lastSnapshotIncludeIndex_;
    }

    DPrintf("[Init&ReInit] Sever %d, term %d, lastSnapshotIncludeIndex {%d} , lastSnapshotIncludeTerm {%d}", 
          raftId_, currentTerm_, lastSnapshotIncludeIndex_, lastSnapshotIncludeTerm_);
    
    mutex_.unlock();
    ioManager_ = std::make_unique<monsoon::IOManager>(FIBER_THREAD_NUM, FIBER_USE_CALLER_THREAD);

    // 启动 ticker 协程来启动选举
    // 启动三个循环定时器
    ioManager_->scheduler([this]() -> void { this->leaderHeartBeatTicker(); });
    ioManager_->scheduler([this]() -> void { this->electionTimeOutTicker(); });

    // 创建一个线程，将新的日志发送到 applyChan_ 中，供上层 KVServer 处理
    std::thread t(&Raft::applierTicker, this);
    t.detach();
}

void Raft::AppendEntries(google::protobuf::RpcController* controller,
                    const ::raftRpcProctoc::AppendEntriesArgs* request,
                    ::raftRpcProctoc::AppendEntriesReply* response,
                    ::google::protobuf::Closure* done)
{
    appendEntriesimpl(request, response);
    done->Run();
}
                    
void Raft::InstallSnapshot(google::protobuf::RpcController* controller,
                    const ::raftRpcProctoc::InstallSnapshotRequest* request,
                    ::raftRpcProctoc::InstallSnapshotResponse* response,
                    ::google::protobuf::Closure* done)
{
    installSnapshotImpl(request, response);
    done->Run();
}

void Raft::RequestVote(google::protobuf::RpcController* controller,
                    const ::raftRpcProctoc::RequestVoteArgs* request,
                    ::raftRpcProctoc::RequestVoteReply* response,
                    ::google::protobuf::Closure* done)
{
    requestVoteImpl(request, response);
    done->Run();
}


// 选举定时器函数，循环执行，直到这个节点成为 Leader 了。
// 每次醒来就检查一次是否超时了，如果超时了就发起选举。
void Raft::electionTimeOutTicker() {
    while(true) {
        while(status_ == Leader) {
            // 为了避免 leader 占用 CPU 一直空转，如果我是 leader 就先睡一会儿，等心跳定时器来唤醒我
            usleep(HeartBeatTimeout);
        }

        std::chrono::duration<signed long long, std::ratio<1, 1000000000>> suitableSleepTime{};
        std::chrono::system_clock::time_point wakeTime;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            wakeTime = now();
            suitableSleepTime = getRandomizedElectionTimeout() + lastResetElectionTime_ - wakeTime;
        }

        // 检查睡眠时间是否合理：如果计算出来的时间大于 1 毫秒，说明确实需要睡眠
        if(std::chrono::duration<double, std::milli>(suitableSleepTime).count() > 1) {

            auto start = std::chrono::steady_clock::now();    // 获取当前时间点，C++ 标准库对 CLOCK_MONOTONIC 的封装
            usleep(std::chrono::duration_cast<std::chrono::microseconds>(suitableSleepTime).count());
            auto end = std::chrono::steady_clock::now();

            std::chrono::duration<double, std::milli> sleepDuration = end - start;

            // 使用ANSI控制序列将输出颜色修改为紫色
            std::cout << "\033[1;35m electionTimeOutTicker();函数设置睡眠时间为: "
                        << std::chrono::duration_cast<std::chrono::milliseconds>(suitableSleepTime).count() << " 毫秒\033[0m"
                        << std::endl;
            std::cout << "\033[1;35m electionTimeOutTicker();函数实际睡眠时间为: " << sleepDuration.count() << " 毫秒\033[0m"
                        << std::endl;
        }

        // 醒来后::检查在我睡觉的这段时间里，定时器被重置了吗？
        // 如果 m_lastResetElectionTime 变大了（比 wakeTime 还大），
        // 那么在我睡觉时，Follower 线程收到了 Leader 的心跳，更新了时间。说明 leader 还活着，我不选举，继续睡觉
        if(std::chrono::duration<double, std::milli>(lastResetElectionTime_ - wakeTime).count() > 0) {
            continue; 
        }

        doElection();   // 否则发起选举
    }
}

// leader 的心跳定时器函数，循环执行，直到这个节点不再是 leader 了。每次醒来就发一次心跳。
void Raft::leaderHeartBeatTicker(){
    while(true) {
        if(status_ != Leader) {
            //不是leader的话就不进行后续操作，因为要拿锁，很影响性能，所以先睡眠
            usleep(1000 * HeartBeatTimeout);
        }

        // 静态原子计数器：static 意味着所有调用共享这个变量，atomic 保证线程安全。用于打印日志
        static std::atomic<int32_t> atomicCount = 0; 

        // 时间区间， 大小使用signed long int存储，单位是ns
        std::chrono::duration<signed long long, std::ratio<1, 1000000000>> suitableSleepTime{};
        std::chrono::system_clock::time_point wakeTime;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            wakeTime = now();
            suitableSleepTime = std::chrono::milliseconds(HeartBeatTimeout) + lastResetHearBeatTime_ - wakeTime;
        }

        // 检查睡眠时间是否合理：如果计算出来的时间大于 1 毫秒，说明确实需要睡眠
        if(std::chrono::duration<double, std::milli>(suitableSleepTime).count() > 1.0) {
            std::cout << atomicCount << "\033[1;35m leaderHearBeatTicker();函数设置睡眠时间为: "
                << std::chrono::duration<double, std::milli>(suitableSleepTime).count() << " 毫秒\033[0m"
                << std::endl;
            
            // 记录开始睡眠的时刻， 睡眠，再次计时，计算实际睡眠的时间
            auto start = std::chrono::steady_clock::now();
            usleep(std::chrono::duration_cast<std::chrono::microseconds>(suitableSleepTime).count());
            auto end = std::chrono::steady_clock::now();

            std::chrono::duration<double, std::milli> sleepDuration = end - start;

            // 打印日志：显示实际睡眠时间，用于调试定时器准不准
            std::cout << atomicCount << "\033[1;35m leaderHearBeatTicker();函数实际睡眠时间为: " << sleepDuration.count()
                        << " 毫秒\033[0m" << std::endl;
            
            atomicCount++;
        }

        // 二次检查（防止虚假唤醒或时间重置）：
        // 再次计算：(上次重置时间 - 刚才醒来的时间)如果 > 0，
        // 说明 m_lastResetHearBeatTime 在我睡觉的时候被更新成了一个未来的时间！
        // 比如：虽然刚睡醒，但可能别的地方同步了日志，所以重置了定时器，所以这次心跳取消，继续睡。
        if(lastResetHearBeatTime_ > wakeTime) {
            continue;
        }

        doHeartBeat();
    }
}

// 这个函数是 leader 真正发送心跳的函数，leaderHeartBeatTicker 里睡醒了就调用它。
// 它会给每个 follower 发 AE RPC。
void Raft::doHeartBeat(){
    std::lock_guard<std::mutex> lock(mutex_);

    if(status_ != Leader) {
        return; // 不是leader就不发心跳了
    }

    DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] Leader的心跳定时器触发了且拿到mutex，开始发送AE\n", raftId_);
    auto appendNum = std::make_shared<int>(1); // 正确返回的节点的数量
    for(int i=0; i<peers_.size(); i++) {
        if(i == raftId_) {
            continue; // 不给自己发心跳
        }

        DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] Leader的心跳定时器触发了 index:{%d}\n", raftId_, i);
        myAssert(nextIndex_[i] >= 1, format("rf.nextIndex_[%d] = {%d}", i, nextIndex_[i]));

        // 判断是发动日志还是发送快照。当 follower 的日志落后太多了，leader 内存里没有了，就只能发送快照了
        if(nextIndex_[i] <= lastSnapshotIncludeIndex_) {
            // 场景：Follower 需要 index=50 的日志，但 Leader 已经把 index=100 之前的日志
            // 都做成快照并删除了。内存里没有 50 号日志，没法发。
            // 动作：开启一个新线程，发送 InstallSnapshot RPC。

            std::thread t(&Raft::leaderSendSnapshot, this, i);
            t.detach(); // 线程分离，自己结束自己，不用 join
            continue;
        }

        // ---------------------------正常情况：构造日志复制请求---------------------------
        //构造发送值,获取 prevLog（前一条日志）的信息
        int preLogIndex = -1;
        int preLogTerm = -1;
        getPrevLogInfo(i, &preLogIndex, &preLogTerm);

        // 填充 Protobuf 消息头
        std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> AppendEntriesArgs = 
                        std::make_shared<raftRpcProctoc::AppendEntriesArgs>();
        AppendEntriesArgs->set_term(currentTerm_);
        AppendEntriesArgs->set_leaderid(raftId_);
        AppendEntriesArgs->set_prevlogindex(preLogIndex);
        AppendEntriesArgs->set_prevlogterm(preLogTerm);
        AppendEntriesArgs->set_leadercommit(commitIndex_);
        AppendEntriesArgs->clear_entries();

        // 填充日志条目 (Entries)
        // 这里的逻辑是：把 logs_ 中，从 preLogIndex 之后的所有日志都打包放进去。
        if(preLogIndex != lastSnapshotIncludeIndex_) {
            // 情况 1：正常情况。通过 getSlicesIndexFromLogIndex 算出 logs_ 数组的下标。
            for(int j = getSlicesIndexFromLogIndex(preLogIndex) + 1; j < logs_.size(); j++) {
                raftRpcProctoc::LogEntry* logEntry = AppendEntriesArgs->add_entries();
                *logEntry = logs_[j];
            }
        } else {
            // 情况 2：Follower 紧挨着快照边缘。
            // 此时 logs_ 里的所有数据都是 Follower 需要的新数据。
            for(const auto& log : logs_) {
                raftRpcProctoc::LogEntry* logEntry = AppendEntriesArgs->add_entries();
                *logEntry = log;
            }
        }

        int lastIndex = getLastLogIndex();
        myAssert(AppendEntriesArgs->prevlogindex() + AppendEntriesArgs->entries_size() == lastIndex,
               format("AppendEntriesArgs.PrevLogIndex{%d}+len(AppendEntriesArgs.Entries){%d} != lastLogIndex{%d}",
                      AppendEntriesArgs->prevlogindex(), AppendEntriesArgs->entries_size(), lastIndex));
        
        //构造返回值
        std::shared_ptr<raftRpcProctoc::AppendEntriesReply> AppendEntriesReply = 
                        std::make_shared<raftRpcProctoc::AppendEntriesReply>();
        AppendEntriesReply->set_appstate(Disconnected); // 先假设是网络断开了，等 RPC 调用成功了再改成正常值

        // 启动一个新的线程来发送这个 RPC 请求。因为 RPC 调用是阻塞的，放在新线程里就不会卡住主线程了。
        std::thread t(&Raft::sendAppendEntries, this, i, AppendEntriesArgs, AppendEntriesReply, appendNum);
        t.detach(); 
    }

    // 更新心跳时间，标记“我刚刚干活了”，这样 leaderHearBeatTicker 就知道什么时候该睡，什么时候该醒。
    lastResetHearBeatTime_ = now();
}


bool Raft::sendAppendEntries(int peerId, std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> args,
                        std::shared_ptr<raftRpcProctoc::AppendEntriesReply> reply,
                        std::shared_ptr<int> appendNums)
{
    DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc开始 ， args->entries_size():{%d}", raftId_,
          peerId, args->entries_size());

    // 发送 RPC 请求（阻塞操作），一直直到 RPC 调用返回了才继续往下走。
    bool isOk = peers_[peerId]->AppendEntries(args.get(), reply.get());
    if(!isOk) {
       DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc失败", raftId_, peerId);
        return false; // RPC 调用失败了，可能是网络问题，直接返回
    }

    DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc成功", raftId_, peerId);
    if(reply->appstate() == Disconnected) {
        return isOk; // RPC 调用成功了，但对方回复说网络断开了，这种情况也当做失败处理，不继续后续逻辑了
    }

    // send 操作返回 成功之后首先先检查对端的任期是否比当前的任期大
    std::lock_guard<std::mutex> lock(mutex_);
    if(reply->term() > currentTerm_) {
        // 发现更高的任期了，说明自己过时了，降级为 follower
        status_ = Follower; 
        currentTerm_ = reply->term();
        votedFor_ = -1; // 过时了，之前投的票也作废了，重置为 -1 表示本轮没有投票
        return isOk;
    } else if(reply->term() < currentTerm_) {
        // 发现更低的任期了，说明对方过时了，直接忽略这个回复就好了，不用处理了
        DPrintf("[func -sendAppendEntries  rf{%d}]  节点：{%d}的term{%d}<rf{%d}的term{%d}\n", 
            raftId_, peerId, reply->term(), raftId_, currentTerm_);
        return isOk;
    }

    // 检查自己的状态
    // 在等待 RPC 回复的期间（锁被释放），我可能已经因为别的原因退位了。
    if(status_ != Leader) {
        return isOk; // 不是leader了，就不处理这个回复了
    }

    // term相等
    myAssert(reply->term() == currentTerm_,
            format("reply.Term{%d} != rf.currentTerm{%d}   ", reply->term(), currentTerm_));
    if(!reply->success()) {
        // 日志不匹配！ 对方在 reply->updatenextindex() 里告诉了我一个建议的回退点。然后我就是更新nextIndex_[peerId]
        if(reply->updatenextindex() != -100) {
            DPrintf("[func -sendAppendEntries  rf{%d}]  返回的日志term相等，但是不匹配，回缩nextIndex[%d]：{%d}\n",
               raftId_, peerId, reply->updatenextindex());
            
            nextIndex_[peerId] = reply->updatenextindex(); // 更新 nextIndex，下次从这个位置重试
        }
    } else {
        *appendNums += 1; // 成功复制了一个节点，计数器加一
        DPrintf("---------------------------tmp------------------------- 节点{%d}返回true,当前*appendNums{%d}",
             peerId, *appendNums);
        
        // 更新同步进度
        // Follower 成功接收了直到 (prevLogIndex + entries_size) 的所有日志。更新 matchIndex 和 nextIndex。
        matchIndex_[peerId] = std::max(matchIndex_[peerId], args->prevlogindex() + args->entries_size());
        nextIndex_[peerId] = matchIndex_[peerId] + 1;
        int lastLogIndex = getLastLogIndex();

        myAssert(nextIndex_[peerId] <= lastLogIndex + 1,
             format("error msg:rf.nextIndex[%d] > lastLogIndex+1, len(rf.logs) = %d   lastLogIndex{%d} = %d",
                    peerId, logs_.size(), peerId, lastLogIndex));
        
        // 检查是不是大多数节点都已经复制了这个日志了，如果是，就可以提交了
        if(*appendNums >= peers_.size() / 2 + 1) {
            *appendNums = 0; // 重置计数器, 保证幂等性
            if(args->entries_size() > 0) {
                DPrintf("args->entries(args->entries_size()-1).logterm(){%d}   currentTerm{%d}",
                    args->entries(args->entries_size() - 1).logterm(), currentTerm_);
            }

            // Raft 安全性规则：只能提交当前任期的日志
            // 如果这次发送的日志（entries）里，最后一条的 term 等于我当前的 term，
            // 说明我在我自己的任期内成功复制了一条日志到大多数节点。
            if(args->entries_size() > 0 && args->entries(args->entries_size() - 1).logterm() == currentTerm_) {
                DPrintf(
                    "---------------------------tmp------------------------- 當前term有log成功提交，更新leader的m_commitIndex "
                    "from{%d} to{%d}",
                    commitIndex_, args->prevlogindex() + args->entries_size());
                
                // 此时，就可以安全地把 commitIndex 推进到这次发送的日志末尾。 要取max
                commitIndex_ = std::max(commitIndex_, args->prevlogindex() + args->entries_size());
            }

            myAssert(commitIndex_ <= lastLogIndex,
               format("[func-sendAppendEntries,rf{%d}] lastLogIndex:%d  rf.commitIndex:%d\n", raftId_, lastLogIndex,
                      commitIndex_));
        }
    }

    return isOk;
}


bool Raft::sendRequestVote(int peerId, std::shared_ptr<raftRpcProctoc::RequestVoteArgs> args,
                            std::shared_ptr<raftRpcProctoc::RequestVoteReply> reply,
                            std::shared_ptr<int> voteNums)
{
    auto start = now();
    DPrintf("[func-sendRequestVote rf{%d}] 向server{%d} 发送 RequestVote 开始", raftId_, peerId);
    bool isOk = peers_[peerId]->RequestVote(args.get(), reply.get());
    DPrintf("[func-sendRequestVote rf{%d}] 向server{%d} 发送 RequestVote 完成，耗时:{%d} ms", 
          raftId_, peerId, std::chrono::duration_cast<std::chrono::milliseconds>(now() - start).count());

    if(!isOk) {
        return isOk; // RPC 调用失败了，可能是网络问题，直接返回
    }

    // send 操作返回 成功之后首先先检查对端的任期是否比当前的任期大
    std::lock_guard<std::mutex> lock(mutex_);
    if(reply->term() > currentTerm_) {
        // 发现更高的任期了，说明自己过时了，降级为 follower
        status_ = Follower;
        currentTerm_ = reply->term();
        votedFor_ = -1;
        persist(); // 记得持久化状态
        return isOk;
    } else if(reply->term() < currentTerm_) {
        return isOk; // 发现更低的任期了，说明对方过时了，直接忽略这个回复就好了，不用处理了
    }

    if(!reply->votegranted()) {
        return isOk; // 对方拒绝了我的投票请求，直接返回就好了
    }

    *voteNums += 1; // 成功获得一个投票，计数加一
    if(*voteNums >= peers_.size() / 2 + 1) {
        *voteNums = 0; // 重置计数器，保证幂等性
        
        if(status_ == Leader) {
            // 不可能已经是 Leader 了
            myAssert(false, format("[func-sendRequestVote-rf{%d}]  term:{%d} 同一个term当两次领导，error", raftId_, currentTerm_));
        }

        status_ = Leader; // 获得了多数节点的选票，成为 Leader
        int lastLogIndex = getLastLogIndex();
        for(int i = 0; i < nextIndex_.size(); i++) {
            nextIndex_[i] = lastLogIndex + 1;   // 初始化下一条要发送的日志的 index
            matchIndex_[i] = 0;                 // 初始化已知的对方已经复制的日志的 index
        }

        // 创建线程开始发送心跳消息
        std::thread t(&Raft::doHeartBeat, this);
        t.detach(); // 线程分离，自己结束自己，不用 join
    }

    return true;
}

// 上层 KVServer 调用这个函数来提交一个新的命令。
// 这个函数会把命令追加到日志里，并返回日志索引、日志任期和是否是 leader。
void Raft::start(Op command, int *newLogIndex, int *newLogTerm, bool *isLeader) {
    // 因为要检查 m_status 和修改 m_logs，必须加锁。
    std::lock_guard<std::mutex> lock(mutex_);

    if(status_ != Leader) {
        DPrintf("[func-Start-rf{%d}]  is not leader", raftId_);
        *newLogIndex = -1;
        *newLogTerm = -1;
        *isLeader = false;
        return; // KVServer 收到 isLeader=false 后，会去找真正的 Leader 重试。
    }

    // 追加日志条目到日志末尾
    raftRpcProctoc::LogEntry newLogEntry;
    newLogEntry.set_command(command.asString());
    newLogEntry.set_logindex(getNewCommandIndex());
    newLogEntry.set_logterm(currentTerm_);
    logs_.emplace_back(newLogEntry); // 追加到日志末尾

    int lastLogIndex = getLastLogIndex();
    DPrintf("[func-Start-rf{%d}]  lastLogIndex:%d,command:%s\n", raftId_, lastLogIndex, &command);
     
    persist(); // 追加了日志，必须持久化状态

    // 获取返回值
    *newLogIndex = lastLogIndex;
    *newLogTerm = currentTerm_;
    *isLeader = true;
}

//Follower（跟随者）接收到 Leader 发来的 AppendEntries 请求后的处理逻辑
void Raft::appendEntriesimpl(const raftRpcProctoc::AppendEntriesArgs* args, 
                                raftRpcProctoc::AppendEntriesReply* reply) 
{
    std::lock_guard<std::mutex> lock(mutex_);
    reply->set_appstate(AppNormal); // RPC 调用成功了，所以这里的网络肯定是正常的

    // 1. Term 检查（硬性规定）: Leader的 Term 比我小？那你是个旧 Leader，一边玩去。
    if(args->term() < currentTerm_) {
        reply->set_term(currentTerm_); // 回复里告诉对方我的 Term，顺便告诉他你过时了
        reply->set_success(false);
        reply->set_updatenextindex(-100);// 告诉对方：你该更新自己了
        DPrintf("[func-AppendEntries-rf{%d}] 拒绝了 因为Leader{%d}的term{%v}< rf{%d}.term{%d}\n", 
            raftId_, args->leaderid(), args->term(), raftId_, currentTerm_);
        return;
    }

    // 2. 持久化：只要走到这一步，说明我们可能会修改 Term 或 Log，必须保证退出函数时持久化状态。
    DEFER { persist(); };

    // 3. 发现新 Leader（三变原则）:对方的 Term 比我大？那我立刻认怂，变 Follower，更新 Term。
    if(args->term() > currentTerm_) {
        status_ = Follower;
        currentTerm_ = args->term();
        votedFor_ = -1; // 过时了，之前投的票也作废了
    }
    myAssert(args->term() == currentTerm_, format("assert {args.Term == rf.currentTerm} fail"));
    
    // 4. 同级 Leader 变身（特殊情况）
    // 对方 Term 等于我，且我和他都是是 Candidate。现在它发心跳了，说明它赢了，我得变 Follower。
    status_ = Follower;
    lastResetElectionTime_ = now(); // 重置选举定时器（保活）

    // Leader 说的 PrevLogIndex 比我的最后一条日志还大？ 我落后了
    if(args->prevlogindex() > getLastLogIndex()) {
        reply->set_term(currentTerm_);
        reply->set_success(false);
        reply->set_updatenextindex(getLastLogIndex() + 1); // 告诉 Leader：我太短了，下次从我的末尾+1 开始试吧。
        return;
    } else if(args->prevlogindex() < lastSnapshotIncludeIndex_) {
        // 2. Leader 说的 PrevLogIndex 在我的快照里？ 这种情况通常不应该发生，除非 Leader 发了很老的包。
        reply->set_term(currentTerm_);
        reply->set_success(false);
        reply->set_updatenextindex(lastSnapshotIncludeIndex_ + 1);
        return; // 原作者这里没加 return，不知道为啥
    }

    // --------------------------核心逻辑——解决冲突与追加日志--------------------------------
    // 调用 matchLog 检查 PrevLogIndex 处的 term 是否等于 args->prevlogterm()
    if(matchLog(args->prevlogindex(), args->prevlogterm())) {
        // === 匹配成功！开始处理日志 entries ===
        for(int i = 0; i < args->entries_size(); i++) {
            auto log = args->entries(i);
            if(log.logindex() > getLastLogIndex()) {
                logs_.push_back(log); // 追加新日志
            } else {
                // 检查是否存在冲突：Index 相同但 Term 不同(其他的是异常情况)。
                if(logs_[getSlicesIndexFromLogIndex(log.logindex())].logterm() == log.logterm() &&
                        logs_[getSlicesIndexFromLogIndex(log.logindex())].command() != log.command()) {
                    //相同位置的log ，其logTerm相等，但是命令却不相同，不符合raft的前向匹配，这是异常情况
                    myAssert(false, format("[func-AppendEntries-rf{%d}] 两节点logIndex{%d}和term{%d}相同，但是其command{%d:%d}   "
                                 " {%d:%d}却不同！！\n",
                                 raftId_, log.logindex(), log.logterm(), raftId_,
                                 logs_[getSlicesIndexFromLogIndex(log.logindex())].command(), args->leaderid(),
                                 log.command()));
                }

                // Term 不一样，说明我的这条日志是旧 Leader 发的脏数据,用 Leader 发来的新数据覆盖它。
                if(logs_[getSlicesIndexFromLogIndex(log.logindex())].logterm() != log.logterm()) {
                    logs_[getSlicesIndexFromLogIndex(log.logindex())] = log; // 冲突了，覆盖掉
                }
            }
        }

        // 处理收到过期的log
        myAssert(args->prevlogindex() + args->entries_size() <= getLastLogIndex(),
                format("[func-AppendEntries1-rf{%d}]rf.getLastLogIndex(){%d} != args.PrevLogIndex{%d}+len(args.Entries){%d}",
               raftId_, getLastLogIndex(), args->prevlogindex(), args->entries_size()));
        
        // 提交日志，更新 commitIndex。 取 min 是因为我可能还没收到 leadercommit 那么新的日志。
        if(args->leadercommit() > commitIndex_) {
            commitIndex_ = std::min(args->leadercommit(), getLastLogIndex());
        }

        myAssert(getLastLogIndex() >= commitIndex_,
             format("[func-AppendEntries1-rf{%d}]  rf.getLastLogIndex{%d} < rf.commitIndex{%d}", 
                    raftId_, getLastLogIndex(), commitIndex_));
        
        reply->set_term(currentTerm_);
        reply->set_success(true);
        return;
    } else {
        // === 匹配失败，告诉 Leader 回退到哪里 ===
        // 使用论文的优化版回退策略
        reply->set_updatenextindex(args->prevlogindex());

        for(int i = args->prevlogindex(); i >= lastSnapshotIncludeIndex_; i--) {
            if(getLogTermFromLogIndex(i) != getLogTermFromLogIndex(args->prevlogindex())) {
                // 回退到上个任期的最后一条。告诉 Leader 下次从这个位置的后一条开始重试。这样能减少 RPC
                reply->set_updatenextindex(i + 1); 
                break;
            }
        }

        reply->set_term(currentTerm_);
        reply->set_success(false);
    }
}

// Follower（跟随者）接收到 Candidate 发来的 RequestVote 请求后的处理逻辑
void Raft::requestVoteImpl(const raftRpcProctoc::RequestVoteArgs* args, 
                            raftRpcProctoc::RequestVoteReply* reply)
{
    std::lock_guard<std::mutex> lock(mutex_);
    DEFER { persist(); }; // 记得持久化状态

    // 检查 leader 发过来的任期是否过时
    if(args->term() < currentTerm_) {
        reply->set_term(currentTerm_);
        reply->set_votegranted(false);  // 过时了，拒绝投票
        reply->set_votestate(Expire);   // 过期了，拒绝投票
        return;
    } else if(args->term() > currentTerm_) {
        status_ = Follower;
        currentTerm_ = args->term();
        votedFor_ = -1; // 过时了，之前投的票也作废了
    }

    myAssert(args->term() == currentTerm_,
             format("[func--rf{%d}] 前面校验过args.Term==rf.currentTerm，这里却不等", raftId_));
    
    // 调用 upToDate 检查日志,如果对方的日志不够新，我就拒绝投票。因为我不想投一个过时的 Leader。
    if(!upToDate(args->lastlogindex(), args->lastlogterm())) {
        reply->set_term(currentTerm_);
        reply->set_votegranted(false);
        reply->set_votestate(Voted);
        return;
    }

    // 4. 检查是否已投票
    // a. 如果 m_votedFor 不是 -1 (表示我已经投过票了)。
    // b. 我投的不是你 (m_votedFor != args->candidateid())。
    //     RPC可能因为网络原因重传，如果我给你投了票，但我的回复丢了，你会重发请求。这时我应该再次同意投票。
    if(votedFor_ != -1 && votedFor_ != args->candidateid()) {
        reply->set_term(currentTerm_);
        reply->set_votegranted(false);
        reply->set_votestate(Voted); // 已经投过票了，拒绝投票
        return;
    } else {
        votedFor_ = args->candidateid(); // 给这个候选人投票
        lastResetElectionTime_ = now(); // 重置选举定时器（保活）,给对方一定时间取竞选

        reply->set_term(currentTerm_);
        reply->set_votegranted(true);
        reply->set_votestate(Normal);
        return;
    }
}

// Follower（跟随者）接收到 Leader 发来的 InstallSnapshot 请求后的处理逻辑
void Raft::installSnapshotImpl(const raftRpcProctoc::InstallSnapshotRequest* args, 
                            raftRpcProctoc::InstallSnapshotResponse* reply)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if(args->term() < currentTerm_) {   // 过时了，拒绝安装快照
        reply->set_term(currentTerm_);
        return;
    }

    if(args->term() > currentTerm_) {   // 发现更高的任期了，说明自己过时了，降级为 follower
        status_ = Follower;
        currentTerm_ = args->term();
        votedFor_ = -1; 
        persist();      // 状态改变了，必须持久化
    }

    status_ = Follower; // 无论如何都要变成 Follower，安装快照了，说明 Leader 活着
    lastResetElectionTime_ = now();

    if(args->lastsnapshotincludeindex() <= lastSnapshotIncludeIndex_) {
        return; // 这是一个延迟到达的旧 RPC 包，我不需要处理它。
    }

    int lastLogIndex = getLastLogIndex();
    if(lastLogIndex > args->lastsnapshotincludeindex()) {
        logs_.erase(logs_.begin(), logs_.begin() + getSlicesIndexFromLogIndex(args->lastsnapshotincludeindex()) + 1);
    } else {
        logs_.clear();
    }

    commitIndex_ = std::max(commitIndex_, args->lastsnapshotincludeindex());
    lastApplied_ = std::max(lastApplied_, args->lastsnapshotincludeindex());
    lastSnapshotIncludeIndex_ = args->lastsnapshotincludeindex();
    lastSnapshotIncludeTerm_ = args->lastsnapshotincludeterm();

    reply->set_term(currentTerm_);
    
    ApplyMsg applyMsg;
    applyMsg.SnapshotValid = true;  // 告诉上层 KVServer 这是一个快照，不是普通日志
    applyMsg.Snapshot = args->data();
    applyMsg.SnapshotIndex = args->lastsnapshotincludeindex();
    applyMsg.SnapshotTerm = args->lastsnapshotincludeterm();

    //
    // 这里使用异步推送，开启一个新线程去推送到 applyChan。
    // 为什么？因为 applyChan->Push 可能会阻塞。如果在当前线程（RPC处理线程）阻塞，
    // 会导致 Leader 收不到回复，一直重试，浪费网络资源。 
    std::thread t(&Raft::pushMsgToKvServer, this, applyMsg);
    t.detach();

    persister_->Save(persistData(), args->data()); // 持久化状态和快照
}

// leader 更新 commitIndex 的函数。
// 每次发送完日志复制 RPC 后，leader 都会调用这个函数来检查有没有新的日志可以提交了。
void Raft::leaderUpdateCommitIndex() {

    // 从后往前扫描日志：
    // 为什么要从最新的日志开始扫？因为 Raft 保证了日志的前向匹配性。
    // 如果 index 100 已经复制到大多数了，那么 99, 98... 也一定复制到大多数了。找到最大的那个 N 即可。
    for(int i=getLastLogIndex(); i > commitIndex_; i--) {
        
        if(getLogTermFromLogIndex(i) != currentTerm_) {
            continue; // 只有当前任期的日志可以触发 commitIndex 的推进
        }

        int count = 1;          // 统计有多少节点复制了这个日志
        for(int j = 0; j < peers_.size(); j++) {
            if(j == raftId_)   continue;

            if(matchIndex_[j] >= i) {
                count++;        // 统计复制了这个日志的节点
            }
        }

        if(count >= peers_.size() / 2 + 1) {
            // 发现一个满足条件的日志了，更新 commitIndex 并返回
            commitIndex_ = i;
            return;
        }
    }
}

// 节点发起选举。当 electionTimeOutTicker 发现超时后，就会调用它
void Raft::doElection() {
    std::lock_guard<std::mutex> lock(mutex_);

    if(status_ == Leader)   return;

    // 2. 只有非 Leader 才能发起选举
    // 如果已经是 Leader 了，就不需要选举了（虽然 Ticker 里面也有判断，这里是双重保险）。
    DPrintf("[       ticker-func-rf(%d)              ]  选举定时器到期且不是leader，开始选举 \n", raftId_);

    status_ = Candidate;
    currentTerm_ += 1; // 每次选举前，任期加1
    votedFor_ = raftId_; // 给自己投票
    persist(); // 选举状态改变了，必须持久化

    std::shared_ptr<int> voteNum = std::make_shared<int>(1); // 选票计数器，初始值为1（因为我已经给自己投了一票了）
    for(int i = 0; i < peers_.size(); i++) {
        if(i == raftId_) {
            continue; // 不给自己发投票请求
        }

        int lastLogIndex = -1, lastLogTerm = -1;
        getLastLogIndexAndTerm(&lastLogIndex, &lastLogTerm);

        std::shared_ptr<raftRpcProctoc::RequestVoteArgs> requestVoteArgs = std::make_shared<raftRpcProctoc::RequestVoteArgs>();
        std::shared_ptr<raftRpcProctoc::RequestVoteReply> voteReply = std::make_shared<raftRpcProctoc::RequestVoteReply>();
        requestVoteArgs->set_term(currentTerm_);
        requestVoteArgs->set_candidateid(raftId_);
        requestVoteArgs->set_lastlogindex(lastLogIndex);
        requestVoteArgs->set_lastlogterm(lastLogTerm);

        // 还是创建一个新线程来发送这个 RPC 请求，避免阻塞主线程。因为如果网络慢了，RPC 调用就会卡住。
        std::thread t(&Raft::sendRequestVote, this, i, requestVoteArgs, voteReply, voteNum);
        t.detach();

    }
}

// 这是一个后台常驻线程（在 init 中被启动）。只要 Raft 节点活着它永远不会停止
// 它的职责就是不断地检查有没有新的日志需要应用到状态机上，如果有就把它们发送到 applyChan_ 里，让 kvserver 应用它们。
void Raft::applierTicker() {
    while(true) {
        std::vector<ApplyMsg> applyMsgs; // 需要应用的日志条目列表

        {
            std::unique_lock<std::mutex> lock(mutex_);
            applyMsgs = getApplyLogs(); // 获取所有需要应用的日志条目

            if (status_ == Leader) {
                DPrintf("[Raft::applierTicker() - raft{%d}]  m_lastApplied{%d}   m_commitIndex{%d}", 
                    raftId_, lastApplied_, commitIndex_);
            }
        }

        if(applyMsgs.empty()) {
            sleepNMilliseconds(ApplyInterval); // 没有需要应用的日志，睡一会儿再检查
            continue;
        }

        DPrintf("[func- Raft::applierTicker()-raft{%d}] 向kvserver报告的applyMsgs长度为：{%d}", raftId_, applyMsgs.size());
        for(const auto& msg : applyMsgs) {
            applyChan_->push(msg); // 把日志条目发送到 applyChan_，让 kvserver 应用它们
        }

        sleepNMilliseconds(ApplyInterval); // 应用完一批日志后，睡一会儿再检查有没有新的日志需要应用
    }
}

// 获取所有需要应用的日志条目。每次调用这个函数，都会把 lastApplied_ 推进到 commitIndex_，
// 并返回这段区间内的日志条目。
std::vector<ApplyMsg> Raft::getApplyLogs() {
    std::vector<ApplyMsg> applyMsgs;

    myAssert(commitIndex_ <= getLastLogIndex(), format("[func-getApplyLogs-rf{%d}] commitIndex{%d} >getLastLogIndex{%d}",
                                                      raftId_, commitIndex_, getLastLogIndex()));

    while(lastApplied_ < commitIndex_) {
        lastApplied_ ++;

        myAssert(logs_[getSlicesIndexFromLogIndex(lastApplied_)].logindex() == lastApplied_,
             format("rf.logs[rf.getSlicesIndexFromLogIndex(rf.lastApplied)].LogIndex{%d} != rf.lastApplied{%d} ",
                    logs_[getSlicesIndexFromLogIndex(lastApplied_)].logindex(), lastApplied_));
        
        ApplyMsg apMsg;
        apMsg.CommandValid = true;  // 这个是一个正常的日志，不是快照
        apMsg.SnapshotValid = false;
        apMsg.Command = logs_[getSlicesIndexFromLogIndex(lastApplied_)].command();
        apMsg.CommandIndex = lastApplied_;

        applyMsgs.emplace_back(apMsg);
    }

    return applyMsgs;
}

// 获取当前节点的状态：任期（term）和是否是 Leader（isLeader）。
// 这个函数会被 kvserver 调用，kvserver 需要知道这些信息来决定如何处理客户端请求。
void Raft::getState(int *term, bool *isLeader) {
    std::lock_guard<std::mutex> lock(mutex_);
    *term = currentTerm_;
    *isLeader = (status_ == Leader);
}

// 上层 KVServer 调用这个函数来把日志条目应用到状态机上。
// 这个函数会把日志条目发送到 applyChan_ 里，让 kvserver 应用它们。
void Raft::pushMsgToKvServer(ApplyMsg msg) {
    applyChan_->push(msg);
}


// 获取 prevLog 的信息：prevLogIndex 和 prevLogTerm
void Raft::getPrevLogInfo(int raftId, int *preIndex, int *preTerm) {
    if(nextIndex_[raftId] == lastSnapshotIncludeIndex_ + 1) {
        *preIndex = lastSnapshotIncludeIndex_;
        *preTerm = lastSnapshotIncludeTerm_;
        return;
    }

    *preIndex = nextIndex_[raftId] - 1;
    *preTerm = logs_[getSlicesIndexFromLogIndex(*preIndex)].logterm();
}

// 根据日志的逻辑 Index 获取搭配日志在 logs_ 数组里面的下标。
// 因为日志的逻辑 Index 是从1开始无限增长的，但是 logs_ 数组的大小是有限制的
int Raft::getSlicesIndexFromLogIndex(int logIndex) {
    myAssert(logIndex > lastSnapshotIncludeIndex_,
        format("[func-getSlicesIndexFromLogIndex-rf{%d}]  index{%d} <= rf.lastSnapshotIncludeIndex{%d}", raftId_,
                  logIndex, lastSnapshotIncludeIndex_));
    int lastLogIndex = getLastLogIndex();
    myAssert(logIndex <= lastLogIndex,
            format("[func-getSlicesIndexFromLogIndex-rf{%d}]  logIndex{%d} > lastLogIndex{%d}", raftId_, logIndex, lastLogIndex));

    // 获得最后一条日志在 logs_ 数组中的下标(逻辑 -> 物理)
    int slicesIndex = logIndex - lastSnapshotIncludeIndex_ - 1;
    return slicesIndex;
}

// 获取最后一个日志的逻辑 Index
int Raft::getLastLogIndex() {
    int lastLogIndex = -1;
    int _ = -1;
    getLastLogIndexAndTerm(&lastLogIndex, &_);
    return lastLogIndex;
}

// 获取最后一个日志的 Term
int Raft::getLastLogTerm(){
    int _ = -1;
    int lastLogTerm = -1;
    getLastLogIndexAndTerm(&_, &lastLogTerm);
    return lastLogTerm;
}

// 找出当前节点拥有的最后一条日志的信息（逻辑 Index 和 Term）
void Raft::getLastLogIndexAndTerm(int *lastLogIndex, int *lastLogTerm){
    if(logs_.empty()) {
        *lastLogIndex = lastSnapshotIncludeIndex_;
        *lastLogTerm = lastSnapshotIncludeTerm_;
        return;
    }

    *lastLogIndex = logs_[logs_.size() - 1].logindex();
    *lastLogTerm = logs_[logs_.size() -1].logterm();
}

// 检查日志匹配：检查 logIndex 处的日志条目的 term 是否等于 logTerm
bool Raft::matchLog(int logIndex, int logTerm) {
    myAssert(logIndex >= lastSnapshotIncludeIndex_ && logIndex <= getLastLogIndex(),
        format("不满足：logIndex{%d}>=rf.lastSnapshotIncludeIndex{%d}&&logIndex{%d}<=rf.getLastLogIndex{%d}",
                  logIndex, lastSnapshotIncludeIndex_, logIndex, getLastLogIndex()));

    return logTerm == getLogTermFromLogIndex(logIndex);
}

// 根据log的逻辑index获取对应的term
int Raft::getLogTermFromLogIndex(int logIndex) {
    myAssert(logIndex >= lastSnapshotIncludeIndex_,
            format("[func-getSlicesIndexFromLogIndex-rf{%d}]  index{%d} < rf.lastSnapshotIncludeIndex{%d}", 
                  raftId_, logIndex, lastSnapshotIncludeIndex_));

    int lastLogIndex = getLastLogIndex();
    myAssert(logIndex <= lastLogIndex, format("[func-getSlicesIndexFromLogIndex-rf{%d}]  logIndex{%d} > lastLogIndex{%d}",
                                            raftId_, logIndex, lastLogIndex));
    if(logIndex == lastSnapshotIncludeIndex_) {
        return lastSnapshotIncludeTerm_;
    } else {
        return logs_[getSlicesIndexFromLogIndex(logIndex)].logterm();
    }
}

// upToDate 函数的逻辑是：
// a. 如果你的 Term 比我的新，那你的日志肯定更新。
// b. 如果 Term 一样，那你的日志必须比我的长或者一样长。
bool Raft::upToDate(int index, int term) {
    int lastLogIndex = -1;
    int lastLogTerm = -1;
    getLastLogIndexAndTerm(&lastLogIndex, &lastLogTerm);
    return term > lastLogTerm || (term == lastLogTerm && index >= lastLogIndex);
}


void Raft::leaderSendSnapshot(int peerId) {
    raftRpcProctoc::InstallSnapshotRequest req;
    raftRpcProctoc::InstallSnapshotResponse resp;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        req.set_leaderid(raftId_);
        req.set_term(currentTerm_);
        req.set_lastsnapshotincludeindex(lastSnapshotIncludeIndex_);
        req.set_lastsnapshotincludeterm(lastSnapshotIncludeTerm_);
        req.set_data(persister_->ReadSnapshot());   // 读取快照数据：从磁盘读出快照二进制数据，塞进请求包。可能会有点慢
    }

    // 发送 InstallSnapshot RPC 请求。这个 RPC 可能会很慢，因为快照数据可能很大。
    bool isOk = peers_[peerId]->InstallSnapshot(&req, &resp);

    std::lock_guard<std::mutex> lock(mutex_);
    if(!isOk) {
        return; // RPC 调用失败了，可能是网络问题，直接返回
    }

    // 状态过期检查：在释放锁的那段时间里，世界可能变了。
    // 如果我现在已经不是 Leader 了，或者我的 Term 变了，那这次发送的结果已经没有意义了，直接作废。
    if(status_ != Leader || currentTerm_ != req.term()) {
        return;
    }

    // 检查 Term 冲突（Raft 核心逻辑）：
    // 如果对方回复的 Term 比我的大，说明对方遇到了更新的 Leader，或者对方本身已经进入了新任期。
    if(resp.term() > currentTerm_) {
        status_ = Follower;
        currentTerm_ = resp.term();
        votedFor_ = -1;

        persist();
        lastResetElectionTime_ = now(); // 重置选举定时器（保活）
        return;
    }

    // 发送成功，对方已经接收了快照，更新我对这个 follower 的认知
    matchIndex_[peerId] = req.lastsnapshotincludeindex();
    nextIndex_[peerId] = matchIndex_[peerId] + 1;
}

// 本来应该在这里实现日志的截断和持久化的，但是这个逻辑已经在 raft 层实现了，所以这里返回 true 就好了
bool Raft::condInstallSnapshot(int lastIncludedTerm, int lastIncludedIndex, std::string snapshot) {
    return true;

    // 在早期的 Raft 实验设计中，安装快照是一个这就麻烦的过程，需要协调 Raft 层 和 Service 层（应用层）。
    // 当 Raft 发现接收到的日志太老，需要安装快照时，它不能直接覆盖整个状态，因为 Service 层可能正在读取数据。
    // 流程通常是：
    // Raft 收到快照，把它打包成 ApplyMsg 发给 Service 层。
    // Service 层收到消息，知道要回滚状态了。
    // Service 层调用 CondInstallSnapshot(回调函数) 告诉 Raft：“我准备好了，且这个快照的 Index 没有旧数据覆盖新数据的问题，你可以把你的日志截断了。”
    // Raft 在这个函数里原子性地截断日志、重置状态。
}

// 这个函数是由上层应用（KVServer）主动调用的，snapshot是已经做好的快照
// 用来告诉 Raft：“我已经把 index 之前的所有状态都做成快照了，你现在可以把旧日志删了
void Raft::snapshot(int index, std::string snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);

    if(lastSnapshotIncludeIndex_ >= index || index > commitIndex_) {
        DPrintf(
            "[func-Snapshot-rf{%d}] rejects replacing log with snapshotIndex %d as current snapshotIndex %d is larger or "
            "smaller ",
            raftId_, index, lastSnapshotIncludeIndex_);
        return; // 这个 index 已经被快照过了，或者还没有提交，没必要处理
    }

    // 保存原始的 快照最后包含的 索引和任期
    int newlastSnapshotIncludeIndex_ = index;
    int newlastSnapshotIncludeTerm_ = getLogTermFromLogIndex(index);

    // 将从 index 之前的日志全部删除掉。 erase(first, last) 删除的是 [first, last) 区间
    logs_.erase(logs_.begin(), logs_.begin() + getSlicesIndexFromLogIndex(index) + 1);
    lastSnapshotIncludeIndex_ = newlastSnapshotIncludeIndex_;
    lastSnapshotIncludeTerm_ = newlastSnapshotIncludeTerm_;
    commitIndex_ = std::max(commitIndex_, index);
    lastApplied_ = std::max(lastApplied_, index);

    persister_->Save(persistData(), snapshot); // 持久化状态和快照

    DPrintf("[SnapShot]Server %d snapshot snapshot index {%d}, term {%d}, loglen {%d}", raftId_, index,
          lastSnapshotIncludeTerm_, logs_.size());
    myAssert(logs_.size() + lastSnapshotIncludeIndex_ == getLastLogIndex(),
            format("len(rf.logs){%d} + rf.lastSnapshotIncludeIndex{%d} != lastLogIndex{%d}", logs_.size(),
                    lastSnapshotIncludeIndex_, getLastLogIndex()));
}


// 创建一个临时的 BoostPersistRaftNode 对象。
// 把 Raft 类的当前状态（currentTerm_, votedFor_ 等）复制到这个临时对象中。
// 使用 boost::archive 把这个临时对象序列化成一个 std::string。
// 返回这个字符串，准备写入磁盘。
std::string Raft::persistData() {
    BoostPersistRaftNode persistNode;
    persistNode.currentTerm = currentTerm_;
    persistNode.votedFor = votedFor_;
    persistNode.lastSnapshotIncludeIndex = lastSnapshotIncludeIndex_;
    persistNode.lastSnapshotIncludeTerm = lastSnapshotIncludeTerm_;
    for(const auto& log : logs_) {  // 复制日志, 日志需要序列化
        persistNode.logs.push_back(log.SerializeAsString());
    }

    std::stringstream ss;
    boost::archive::text_oarchive oa(ss);
    oa << persistNode;   // 把 persistNode 序列化到 ss 中
    return ss.str();     // 返回序列化后的字符串
}

// 获取新命令应该分配的Index。这个 Index 是全局递增的，等于最后一条日志的 Index + 1。
int Raft::getNewCommandIndex() {
    auto lastLogIndex = getLastLogIndex();
    return lastLogIndex + 1;
}

// 获取状态字符串序列化之后的长度，这个长度会被 RaftNode 用来判断磁盘里有没有数据可读。
int Raft::getRaftStateSize() {  return persister_->RaftStateSize(); }

// 接收一个从磁盘读出来的字符串 data。
// 使用 boost::archive 把这个字符串反序列化，填充到一个临时的 BoostPersistRaftNode 对象中。
// 把临时对象里的数据复制回 Raft 类的成员变量（currentTerm_, votedFor_ 等），完成状态恢复。
void Raft::readPersist(std::string data) {
    if(data.empty()) {
        return; // 没有数据可读，直接返回
    }

    BoostPersistRaftNode persistNode;
    std::stringstream ss(data);
    boost::archive::text_iarchive ia(ss);
    ia >> persistNode;   // 从 ss 中反序列化到 persistNode

    // 再将 persistNode 中的数据复制回 Raft 类的成员变量
    currentTerm_ = persistNode.currentTerm;
    votedFor_ = persistNode.votedFor;
    lastSnapshotIncludeIndex_ = persistNode.lastSnapshotIncludeIndex;
    lastSnapshotIncludeTerm_ = persistNode.lastSnapshotIncludeTerm;
    logs_.clear();
    for(const auto& log : persistNode.logs) {
        raftRpcProctoc::LogEntry logEntry;  // 日志需要反序列化
        logEntry.ParseFromString(log);  
        logs_.push_back(logEntry);
    }

}

void Raft::persist() {
    auto data = persistData(); // 获取当前状态的序列化字符串
    persister_->SaveRaftState(data); // 写入磁盘
}
