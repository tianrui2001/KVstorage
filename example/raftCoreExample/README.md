# 关于 raft KVDB 的测试

**进入到 ./bin 目录下面**:

```bash
cd ./bin
```

**打开一个终端，然后执行下面的命令：**

```bash
./raftCoreRun -n 3 -f test.conf
```

``test.conf`` 存放的是 ``raft`` 集群的配置信息，必须加载这个文件

**另起一个终端，执行 500 次 Put 操作，然后再执行 500 次Get 操作**

```bash
./callerMain
```



## 执行结果

**终端输出：**

```bash
[2026-3-6-17-28-34][func-MprpcChannel::CallMethod]连接ip：{127.0.1.1} port{16011}成功
[2026-3-6-17-28-34][func-MprpcChannel::CallMethod]连接ip：{127.0.1.1} port{16012}成功
[2026-3-6-17-28-34][func-MprpcChannel::CallMethod]连接ip：{127.0.1.1} port{16013}成功
Get x: 499
Get x: 498
Get x: 497
Get x: 496
……
Get x: 2
Get x: 1
Get x: 0
```

执行一次 Put 操作然后再从 ``KVDB`` 里面取出刚刚插入的数据，可以看到500条数据都成功取出了。

**raft 集群日志**

```bach
[2026-3-6-17-29-3][Raft::applierTicker() - raft{2}]  m_lastApplied{1000}   m_commitIndex{1000}
[2026-3-6-17-29-3][Raft::applierTicker() - raft{2}]  m_lastApplied{1000}   m_commitIndex{1000}
```

可以看到，执行完只会每个 ``raft`` 节点的 ``lastApplied`` 和 ``commitIndex`` 均为1000，所以500条 ``Put`` 操作和500条 ``Get`` 操作成功完成

**中间某些过程的日志：**

```bash
***** Skip List *****
Level0: (x, 195)
Level1: (x, 195)
Level2: (x, 195)
Level3: (x, 195)
Level4: (x, 195)
[2026-3-6-17-28-49][RaftApplyMessageSendToWaitChan--> raftserver{2}] , Send Command --> Index:{609} , ClientId {1526646960}, RequestId {609}, Opreation {%v}, Key :{%v}, Value :{%v}
[2026-3-6-17-28-49][func -KvServer::PutAppend -kvserver{2}]WaitChanGetRaftApplyMessage<--Server 2 , get Command <-- Index:609 , ClientId , RequestId 609, Opreation �:q^, Key :�:q^, Value :�:q^
[2026-3-6-17-28-49][func-Start-rf{2}]  lastLogIndex:610,command:@:q^

[2026-3-6-17-28-49][Raft::applierTicker() - raft{2}]  m_lastApplied{609}   m_commitIndex{609}
940[1;35m leaderHearBeatTicker();函数实际睡眠时间为: 23.6828 毫秒[0m
[2026-3-6-17-28-49][func-Raft::doHeartBeat()-Leader: {2}] Leader的心跳定时器触发了且拿到mutex，开始发送AE

[2026-3-6-17-28-49][func-Raft::doHeartBeat()-Leader: {2}] Leader的心跳定时器触发了 index:{0}

[2026-3-6-17-28-49][func-Raft::doHeartBeat()-Leader: {2}] Leader的心跳定时器触发了 index:{1}

[2026-3-6-17-28-49][func-Raft::sendAppendEntries-raft{2}] leader 向节点{0}发送AE rpc开始 ， args->entries_size():{1}
941[1;35m leaderHearBeatTicker();函数设置睡眠时间为: 24.9957 毫秒[0m
[2026-3-6-17-28-49][func- Raft::applierTicker()-raft{1}] 向kvserver报告的applyMsgs长度为：{1}
[2026-3-6-17-28-49]---------------tmp-------------[func-KvServer::ReadRaftApplyCommandLoop()-kvserver{1}] 收到了下raft的消息
[2026-3-6-17-28-49][KvServer::GetCommandFromRaft-kvserver{1}] , Got Command --> Index:{608} , ClientId {P`h^}, RequestId {608}, Opreation {`��Z^}, Key :{���Z^}, Value :{���Z^}
[2026-3-6-17-28-49][SnapShot]Server 1 snapshot snapshot index {608}, term {1}, loglen {1}
[2026-3-6-17-28-49][func- Raft::applierTicker()-raft{1}] 向kvserver报告的applyMsgs长度为：{1}
[2026-3-6-17-28-49]---------------tmp-------------[func-KvServer::ReadRaftApplyCommandLoop()-kvserver{1}] 收到了下raft的消息
[2026-3-6-17-28-49][KvServer::GetCommandFromRaft-kvserver{1}] , Got Command --> Index:{609} , ClientId {0Nh^}, RequestId {609}, Opreation {`��Z^}, Key :{���Z^}, Value :{���Z^}
search_element-----------------
Found key: x, value: 196
Successfully deleted key: x
Successfully inserted key:x, value:195

***** Skip List *****
Level0: (x, 195)
Level1: (x, 195)
Level2: (x, 195)
Level3: (x, 195)
Level4: (x, 195)
[2026-3-6-17-28-49][func-Raft::sendAppendEntries-raft{2}] leader 向节点{1}发送AE rpc开始 ， args->entries_size():{1}
[2026-3-6-17-28-49][func-Raft::sendAppendEntries-raft{2}] leader 向节点{0}发送AE rpc成功
[2026-3-6-17-28-49]---------------------------tmp------------------------- 节点{0}返回true,当前*appendNums{2}
[2026-3-6-17-28-49]args->entries(args->entries_size()-1).logterm(){1}   currentTerm{1}
[2026-3-6-17-28-49]---------------------------tmp------------------------- 當前term有log成功提交，更新leader的m_commitIndex from{609} to{610}
[2026-3-6-17-28-49][func-Raft::sendAppendEntries-raft{2}] leader 向节点{1}发送AE rpc成功
[2026-3-6-17-28-49]---------------------------tmp------------------------- 节点{1}返回true,当前*appendNums{1}
[2026-3-6-17-28-49][Raft::applierTicker() - raft{2}]  m_lastApplied{610}   m_commitIndex{610}
[2026-3-6-17-28-49][func- Raft::applierTicker()-raft{2}] 向kvserver报告的applyMsgs长度为：{1}
[2026-3-6-17-28-49]---------------tmp-------------[func-KvServer::ReadRaftApplyCommandLoop()-kvserver{2}] 收到了下raft的消息
[2026-3-6-17-28-49][KvServer::GetCommandFromRaft-kvserver{2}] , Got Command --> Index:{610} , ClientId {`_}, RequestId {610}, Opreation {`��Z^}, Key :{���Z^}, Value :{���Z^}
[2026-3-6-17-28-49][SnapShot]Server 2 snapshot snapshot index {610}, term {1}, loglen {0}
[2026-3-6-17-28-49][RaftApplyMessageSendToWaitChan--> raftserver{2}] , Send Command --> Index:{610} , ClientId {1526646960}, RequestId {610}, Opreation {%v}, Key :{%v}, Value :{%v}
search_element-----------------
Found key: x, value: 195
```



