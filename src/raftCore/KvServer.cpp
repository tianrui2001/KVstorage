
#include "KvServer.h"
#include "rpcprovider.h"
#include "raftRpcUtil.h"
#include "mprpcconfig.h"

KvServer::KvServer(int raftId, int maxRaftStateSize, std::string nodeInfoFileName, short port)
    : raftId_(raftId), maxRaftStateSize_(maxRaftStateSize), skipList_(6)
{
    std::shared_ptr<Persister> persister = std::make_shared<Persister>(raftId);
    applyChan_ = std::make_shared<LockQueue<ApplyMsg>>();
    raftNode_ = std::make_shared<Raft>();

    //  注册RPC服务：clerk层面 kvserver开启rpc接受功能
    //  同时raft与raft节点之间也要开启rpc功能，因此有两个注册
    std::thread t([this, port]() -> void {
        RpcProvider provider;
        provider.NotifyService(this);
        provider.NotifyService(raftNode_.get());

        // 启动一个rpc服务发布节点   Run以后，进程进入阻塞状态，等待远程的rpc调用请求
        provider.Run(raftId_, port);
    });
    t.detach();

    std::cout << "raftServer node:" << raftId_ << " start to sleep to wait all ohter raftnode start!!!!" << std::endl;
    sleep(6); // 等待RPC服务启动完成，避免后续的RPC调用失败
    std::cout << "raftServer node:" << raftId_ << " wake up!!!! start to connect other raftnode" << std::endl;

    // 读取其他所有对端节点的配置文件
    MrpcConfig conf;
    conf.LoadConfigFile(nodeInfoFileName.c_str());
    std::vector<std::pair<std::string, short>> peerAddrs;
    for(int i = 0; i < INT_MAX - 1; i++) {
        std::string node = "node" + std::to_string(i);

        std::string nodeIp = conf.Load(node + "ip");
        std::string nodePortStr = conf.Load(node + "port");
        if(nodeIp.empty()) {
            break; // 没有更多的节点了，退出循环
        }

        peerAddrs.emplace_back(nodeIp, static_cast<short>(std::stoi(nodePortStr)));
    }

    // 创建 RaftRPCUtil 对象，连接其他所有节点的 RPC 服务
    std::vector<std::shared_ptr<RaftRPCUtil>> peers;
    for(int i = 0; i < peerAddrs.size(); i++) {
        if(i == raftId_) {
            peers.push_back(nullptr); // 自己的位置放一个空指针占位
            continue;
        }

        RaftRPCUtil* rpcUtil = new RaftRPCUtil(peerAddrs[i].first, peerAddrs[i].second);
        peers.push_back(std::shared_ptr<RaftRPCUtil>(rpcUtil));
    }

    // 等待所有的节点链接成功
    sleep(peerAddrs.size() - raftId_);

    // 初始化 raft 节点
    raftNode_->init(peers, raftId_, persister, applyChan_);

    // kvdb初始化
    lastSnapShotRaftLogIndex_ = 0;
    auto snapshotData = persister->ReadSnapshot();
    if(!snapshotData.empty()) { // 如果读取到快照数据，那么加载到跳表中
        readSnapshotToInstall(snapshotData);
    }

    std::thread t1(&KvServer::readApplyCommandLoop, this);  // 启动一个后台线程，不断地从 applyChan_ 中读取已经提交的日志，并应用到状态机上
    t1.detach();
}

void KvServer::dprintfKVDB() {
    if(!Debug) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    skipList_.displayList();
}

// 这个函数会在 raft 节点安装快照的时候被调用。它的作用是向跳表的某个节点追加val。
void KvServer::execAppendOpOnKVDB(Op op) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string oldVal;
        if(skipList_.searchElement(op.Key, oldVal)) {
            skipList_.insertSetElement(op.Key, oldVal + op.Value); // 将Op的新值追加在旧值的后面
        } else {
            skipList_.insertSetElement(op.Key, op.Value); // 之前没有这个键，直接插入新的值
        }

        lastRequestIdMap_[op.ClientId] = op.RequestId; // 更新这个客户端的最后请求ID
    }

    dprintfKVDB();
}

// 从跳表中查找键值对，并将结果保存在 value 和 isExist 中。这个函数会在处理 Get 请求的时候被调用。
void KvServer::execGetOpOnKVDB(Op op, std::string* value, bool* isExist) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        *value = "";
        *isExist = skipList_.searchElement(op.Key, *value);
        lastRequestIdMap_[op.ClientId] = op.RequestId; // 更新这个客户端的最后请求ID
    }

    dprintfKVDB();
}

// 向跳表中插入或更新一个键值对。这个函数会在处理 Put 请求的时候被调用。
void KvServer::execPutOpOnKVDB(Op op) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        skipList_.insertSetElement(op.Key, op.Value);
        lastRequestIdMap_[op.ClientId] = op.RequestId; // 更新这个客户端的最后请求ID
    }

    dprintfKVDB();
}

// 这里是从快照中读取数据，并将数据全部加载到跳表中。这个函数会在 raft 节点安装快照的时候被调用。
void KvServer::readSnapshotToInstall(std::string snapshot) {
    if(snapshot.empty()) {
        return;
    }

    loadFromSnapshotData(snapshot);
}

// 这个是 Get 请求的实现函数。
// 当一个 Get 请求到达时，KvServer 会调用这个函数来处理这个请求。
// 它会先将这个请求封装成一个 Op 对象，然后调用 raftNode_->start() 将这个 Op 提交给 Raft 节点进行共识。
// 接下来它会等待这个日志被提交并应用到状态机上，最后再从跳表中读取结果返回给客户端。
void KvServer::getImpl(const raftKVRpcProctoc::GetArgs* args, raftKVRpcProctoc::GetReply* reply) {
    Op op;
    op.Operation = "Get";
    op.Key = args->key();
    op.ClientId = args->clientid();
    op.RequestId = args->requestid();
    op.Value = "";

    int raftLogIndex = -1;
    int _ = -1;
    bool isLeader = false;
    raftNode_->start(op, &raftLogIndex, &_, &isLeader);

    // 如果当前节点不是leader，那么直接返回错误，不需要等待日志被提交了
    if(!isLeader) {
        reply->set_err(ErrWrongLeader);
        return;
    }

    // 创建对应的等待队列，并获取这个等待队列的指针
    LockQueue<Op>* chForRaftIndex = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 如果这个日志索引还没有对应的等待队列，那么创建一个新的等待队列
        if(waitApplyCh_.find(raftLogIndex) == waitApplyCh_.end()) {
            waitApplyCh_[raftLogIndex] = new LockQueue<Op>();
        }
        chForRaftIndex = waitApplyCh_[raftLogIndex];
    }

    // 等待等待队列pop是否超时
    Op raftCommitOp;
    if(!chForRaftIndex->timeOutPop(ConsensusTimeout, &raftCommitOp)) {
        // 如果 chForRaftIndex 等待超时， 说明这个日志可能永远也提交不了了
        // 可能是因为网络分区了，或者这个leader已经挂了，这时候直接返回错误给客户端，不需要继续等下去了
        int _ = -1;
        bool isLeader = false;
        raftNode_->getState(&_, &isLeader);

        if(isLeader && isRequestDuplicate(op.ClientId, op.RequestId)) {
            // 如果是重复的且自己还是 Leader，为了用户体验会尝试直接从本地读。
            std::string value;
            bool exist;
            execGetOpOnKVDB(op, &value, &exist);
            if(exist) { 
                // 之前有这个键，说明之前这个请求是成功过的，那么这次重复请求也应该成功，直接返回之前的值就好了
                reply->set_value(value);
                reply->set_err(OK);
            } else {
                reply->set_value("");
                reply->set_err("ErrNoKey");
            }
        } else {
            reply->set_err(ErrWrongLeader); //返回这个，其实就是让clerk换一个节点重试
        }
    } else {
        // 情况 B：收到通知（后台 Apply 线程已经执行到了这条日志）
        // 为什么要再次比对：因为在 Raft 中，即使 raftLogIndex 处提交了日志，由于切主等原因，该索引处的指令可能已经变成了别人的指令。
        // 只有 ClientId 和 RequestId 都匹配，才说明这真的是我刚才发起的那个 Get 请求
        if(raftCommitOp.ClientId == op.ClientId && raftCommitOp.RequestId == op.RequestId) {
            std::string value;
            bool exist;
            execGetOpOnKVDB(op, &value, &exist);
            if(exist) {
                reply->set_value(value);
                reply->set_err(OK);
            } else {
                reply->set_value("");
                reply->set_err("ErrNoKey");
            }
        } else {
            // 说明这个日志虽然提交了，但不是我这个请求的日志，可能是切主了，
            // 之前的日志被新的日志覆盖了，这时候让clerk重试就好了
            reply->set_err(ErrWrongLeader); 
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto applyCh = waitApplyCh_[raftLogIndex];
        waitApplyCh_.erase(raftLogIndex);
        delete applyCh; // 这个等待队列不需要了，删除掉，避免内存泄漏
    }
}

// clerk 使用RPC远程调用
void KvServer::putAppendImpl(const raftKVRpcProctoc::PutAppendArgs* args, 
                    raftKVRpcProctoc::PutAppendReply* reply) 
{
    Op op;
    op.Operation = args->op();
    op.Key = args->key();
    op.Value = args->value();
    op.ClientId = args->clientid();
    op.RequestId = args->requestid();

    int raftLogIndex = -1;
    int _ = -1;
    bool isLeader = false;
    raftNode_->start(op, &raftLogIndex, &_, &isLeader);

    if(!isLeader) {
        reply->set_err(ErrWrongLeader);
        return;
    }

    LockQueue<Op>* chForRaftIndex = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(waitApplyCh_.find(raftLogIndex) == waitApplyCh_.end()) {
            waitApplyCh_[raftLogIndex] = new LockQueue<Op>();
        }
        chForRaftIndex = waitApplyCh_[raftLogIndex];
    }

    Op raftCommitOp;
    if(chForRaftIndex->timeOutPop(ConsensusTimeout, &raftCommitOp)) {
        DPrintf(
            "[func -KvServer::PutAppend -kvserver{%d}]TIMEOUT PUTAPPEND !!!! Server %d , get Command <-- Index:%d , "
            "ClientId %s, RequestId %s, Opreation %s Key :%s, Value :%s",
            raftId_, raftId_, raftLogIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);

        if(isRequestDuplicate(op.ClientId, op.RequestId)) {
            reply->set_err(OK); // 之前这个请求成功过了，这次重复请求也应该成功，直接返回 OK 就好了
        } else {
            reply->set_err(ErrWrongLeader); ///这里返回这个的目的让clerk重新尝试
        }
    } else {
        DPrintf(
            "[func -KvServer::PutAppend -kvserver{%d}]WaitChanGetRaftApplyMessage<--Server %d , get Command <-- Index:%d , "
            "ClientId %s, RequestId %d, Opreation %s, Key :%s, Value :%s",
            raftId_, raftId_, raftLogIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);

        // 可能发送 leader 变更，必须要检查
        if(raftCommitOp.ClientId == op.ClientId && raftCommitOp.RequestId == op.RequestId) {
            reply->set_err(OK);
        } else {
            reply->set_err(ErrWrongLeader);
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto applyCh = waitApplyCh_[raftLogIndex];
        waitApplyCh_.erase(raftLogIndex);
        delete applyCh; // 这个等待队列不需要了，删除掉，避免内存泄漏 
    }
}

// 这个函数会在 Raft 节点应用日志到状态机的时候被调用。
// 它的作用是从 Raft 的 ApplyMsg 中获取 Op，然后执行对应的操作，并且通知正在等待这个日志被提交的线程。
void KvServer::getCommandFromRaft(ApplyMsg msg) {
    Op op;
    op.parseFromString(msg.Command);    // 反序列化 command

    DPrintf(
      "[KvServer::GetCommandFromRaft-kvserver{%d}] , Got Command --> Index:{%d} , ClientId {%s}, RequestId {%d}, "
      "Opreation {%s}, Key :{%s}, Value :{%s}",
      raftId_, msg.CommandIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);

    if(lastSnapShotRaftLogIndex_ >= msg.CommandIndex) {
        // 如果这条消息的索引已经包含在之前的快照中了，说明对应的状态已经反映在数据库里，直接跳过
        return;
    }

    // put 和 append 操作不能重复执行，否则就会破坏数据，所以需要判断一下这个请求是不是重复请求。
    if(!isRequestDuplicate(op.ClientId, op.RequestId)) {
        if(op.Operation == "Put") {
            execPutOpOnKVDB(op);
        } else if(op.Operation == "Append") {
            execAppendOpOnKVDB(op);
        }
    }

    // maxRaftStateSize_!=-1表示开启了快照功能，每应用一条日志就检查一下是否超过阈值，那么就触发制作快照
    if(maxRaftStateSize_ != -1) {
        ifNeedToSendSnapshotCommand(msg.CommandIndex, 9); // 这里设置成90%了，避免频繁触发快照
    }

    // 通知正在等待这个日志被提交的线程，这个日志已经提交了
    // 无论 op 是 Put, Append 还是 Get，只要是从 Raft 传回来的消息，最后都会执行这一句：
    sendMsgToWaitChan(op, msg.CommandIndex);
}


void KvServer::getSnapshotFromRaft(ApplyMsg msg) {
    std::lock_guard<std::mutex> lock(mutex_);

    if(raftNode_->condInstallSnapshot(msg.SnapshotTerm, msg.SnapshotIndex, msg.Snapshot)) {
        // 如果安装快照成功了，那么就把快照中的数据加载到跳表中，
        // 并且更新 lastSnapShotRaftLogIndex_，让它等于这个快照的 Raft 日志索引
        readSnapshotToInstall(msg.Snapshot);
        lastSnapShotRaftLogIndex_ = msg.SnapshotIndex;
    }
}

// 用于判断是否要压缩日志，如果开启了快照，并且当前 Raft 状态的大小已经超过了阈值，
// 那么就制作快照并告诉 Raft 可以截断日志了。然后raft进行相应的处理，删除旧日志，保留快照和之后的日志。
void KvServer::ifNeedToSendSnapshotCommand(int raftLogIndex, int proportion) {
    if(maxRaftStateSize_ == -1) {
        return; // 没有开启快照功能
    }

    if(raftNode_->getRaftStateSize() >= maxRaftStateSize_ * proportion / 10.0) {
        auto snapshotData = makeSnapshot();
        raftNode_->snapshot(raftLogIndex, snapshotData);
    }
}

// 安全地获取当前状态的快照数据。这个函数会在制作快照的时候被调用。
std::string KvServer::makeSnapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return getSnapshotData();
}

// 向等待队列发送消息，并通知正在等待这个日志被提交的线程。
bool KvServer::sendMsgToWaitChan(const Op& op, int raftLogIndex) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(waitApplyCh_.find(raftLogIndex) == waitApplyCh_.end()) {
        return false;
    }

    DPrintf(
      "[RaftApplyMessageSendToWaitChan--> raftserver{%d}] , Send Command --> Index:{%d} , ClientId {%d}, RequestId "
      "{%d}, Opreation {%v}, Key :{%v}, Value :{%v}",
      raftId_, raftLogIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);

    // 如果找到了对应的等待队列，那么就把这个 Op 推送到等待队列里，通知正在等这个日志被提交的线程
    waitApplyCh_[raftLogIndex]->push(op);
    return true;
}

// 判断一个客户端的请求是不是重复请求。这个函数会在处理 Get 和 Put 请求的时候被调用，以保证幂等性。
bool KvServer::isRequestDuplicate(std::string clientId, int requestId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(lastRequestIdMap_.find(clientId) == lastRequestIdMap_.end()) {
        return false; // 之前没有这个客户端的请求记录，说明不是重复请求
    }

    // 如果请求ID小于等于记录的最后请求ID，说明是重复请求
    return requestId <= lastRequestIdMap_[clientId]; 
}

//一直等待raft传来的applyCh
void KvServer::readApplyCommandLoop() {
    while(true) {
        //如果只操作applyChan不用拿锁，因为applyChan自己带锁
        auto msg = applyChan_->pop();

         DPrintf(
            "---------------tmp-------------[func-KvServer::ReadRaftApplyCommandLoop()-kvserver{%d}] 收到了下raft的消息",
            raftId_);
        
        if(msg.CommandValid) {
            getCommandFromRaft(msg);
        }

        if(msg.SnapshotValid) {
            getSnapshotFromRaft(msg);
        }
    }
}

void KvServer::PutAppend(google::protobuf::RpcController* controller,
                const ::raftKVRpcProctoc::PutAppendArgs* request,
                ::raftKVRpcProctoc::PutAppendReply* response,
                ::google::protobuf::Closure* done)
{
    putAppendImpl(request, response);
    done->Run();
}

void KvServer::Get(google::protobuf::RpcController* controller,
                const ::raftKVRpcProctoc::GetArgs* request,
                ::raftKVRpcProctoc::GetReply* response,
                ::google::protobuf::Closure* done)
{
    getImpl(request, response);
    done->Run();
}