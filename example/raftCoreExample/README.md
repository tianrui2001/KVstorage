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

## 插入500条不同的数据

**以插入x398, value: 398为例：**

```bash
[2026-3-9-19-12-50][Raft::applierTicker() - raft{1}]  m_lastApplied{202}   m_commitIndex{202}
575[1;35m leaderHearBeatTicker();函数实际睡眠时间为: 23.7693 毫秒[0m
[2026-3-9-19-12-50][func-Raft::doHeartBeat()-Leader: {1}] Leader的心跳定时器触发了且拿到mutex，开始发送AE

[2026-3-9-19-12-50][func-Raft::doHeartBeat()-Leader: {1}] Leader的心跳定时器触发了 index:{0}

[2026-3-9-19-12-50][func-Raft::doHeartBeat()-Leader: {1}] Leader的心跳定时器触发了 index:{2}

[2026-3-9-19-12-50][func-Raft::sendAppendEntries-raft{1}] leader 向节点{0}发送AE rpc开始 ， args->entries_size():{1}
576[1;35m leaderHearBeatTicker();函数设置睡眠时间为: 24.9957 毫秒[0m
[2026-3-9-19-12-50][func-Raft::sendAppendEntries-raft{1}] leader 向节点{2}发送AE rpc开始 ， args->entries_size():{1}
[2026-3-9-19-12-50][func-Raft::sendAppendEntries-raft{1}] leader 向节点{0}发送AE rpc成功
[2026-3-9-19-12-50]---------------------------tmp------------------------- 节点{0}返回true,当前*appendNums{2}
[2026-3-9-19-12-50]args->entries(args->entries_size()-1).logterm(){1}   currentTerm{1}
[2026-3-9-19-12-50]---------------------------tmp------------------------- 當前term有log成功提交，更新leader的m_commitIndex from{202} to{203}
[2026-3-9-19-12-50][func-Raft::sendAppendEntries-raft{1}] leader 向节点{2}发送AE rpc成功
[2026-3-9-19-12-50]---------------------------tmp------------------------- 节点{2}返回true,当前*appendNums{1}
[2026-3-9-19-12-50][Raft::applierTicker() - raft{1}]  m_lastApplied{203}   m_commitIndex{203}
[2026-3-9-19-12-50][func- Raft::applierTicker()-raft{1}] 向kvserver报告的applyMsgs长度为：{1}
[2026-3-9-19-12-50]---------------tmp-------------[func-KvServer::ReadRaftApplyCommandLoop()-kvserver{1}] 收到了下raft的消息
[2026-3-9-19-12-50][KvServer::GetCommandFromRaft-kvserver{1}] , Got Command --> Index:{203} , ClientId {}, RequestId {203}, Opreation {`��t�}, Key :{���t�}, Value :{���t�}
search_element-----------------
Not Found Key:x398
Successfully inserted key:x398, value:398

***** Skip List *****
Level0: (x398, 398) (x399, 399) (x400, 400) (x401, 401) (x402, 402) (x403, 403) (x404, 404) (x405, 405) (x406, 406) (x407, 407) (x408, 408) (x409, 409) (x410, 410) (x411, 411) (x412, 412) (x413, 413) (x414, 414) (x415, 415) (x416, 416) (x417, 417) (x418, 418) (x419, 419) (x420, 420) (x421, 421) (x422, 422) (x423, 423) (x424, 424) (x425, 425) (x426, 426) (x427, 427) (x428, 428) (x429, 429) (x430, 430) (x431, 431) (x432, 432) (x433, 433) (x434, 434) (x435, 435) (x436, 436) (x437, 437) (x438, 438) (x439, 439) (x440, 440) (x441, 441) (x442, 442) (x443, 443) (x444, 444) (x445, 445) (x446, 446) (x447, 447) (x448, 448) (x449, 449) (x450, 450) (x451, 451) (x452, 452) (x453, 453) (x454, 454) (x455, 455) (x456, 456) (x457, 457) (x458, 458) (x459, 459) (x460, 460) (x461, 461) (x462, 462) (x463, 463) (x464, 464) (x465, 465) (x466, 466) (x467, 467) (x468, 468) (x469, 469) (x470, 470) (x471, 471) (x472, 472) (x473, 473) (x474, 474) (x475, 475) (x476, 476) (x477, 477) (x478, 478) (x479, 479) (x480, 480) (x481, 481) (x482, 482) (x483, 483) (x484, 484) (x485, 485) (x486, 486) (x487, 487) (x488, 488) (x489, 489) (x490, 490) (x491, 491) (x492, 492) (x493, 493) (x494, 494) (x495, 495) (x496, 496) (x497, 497) (x498, 498) (x499, 499)
Level1: (x398, 398) (x399, 399) (x400, 400) (x401, 401) (x402, 402) (x403, 403) (x404, 404) (x405, 405) (x406, 406) (x407, 407) (x408, 408) (x409, 409) (x410, 410) (x411, 411) (x412, 412) (x413, 413) (x414, 414) (x415, 415) (x416, 416) (x417, 417) (x418, 418) (x419, 419) (x420, 420) (x421, 421) (x422, 422) (x423, 423) (x424, 424) (x425, 425) (x426, 426) (x427, 427) (x428, 428) (x429, 429) (x430, 430) (x431, 431) (x432, 432) (x433, 433) (x434, 434) (x435, 435) (x436, 436) (x437, 437) (x438, 438) (x439, 439) (x440, 440) (x441, 441) (x442, 442) (x443, 443) (x444, 444) (x445, 445) (x446, 446) (x447, 447) (x448, 448) (x449, 449) (x450, 450) (x451, 451) (x452, 452) (x453, 453) (x454, 454) (x455, 455) (x456, 456) (x457, 457) (x458, 458) (x459, 459) (x460, 460) (x461, 461) (x462, 462) (x463, 463) (x464, 464) (x465, 465) (x466, 466) (x467, 467) (x468, 468) (x469, 469) (x470, 470) (x471, 471) (x472, 472) (x473, 473) (x474, 474) (x475, 475) (x476, 476) (x477, 477) (x478, 478) (x479, 479) (x480, 480) (x481, 481) (x482, 482) (x483, 483) (x484, 484) (x485, 485) (x486, 486) (x487, 487) (x488, 488) (x489, 489) (x490, 490) (x491, 491) (x492, 492) (x493, 493) (x494, 494) (x495, 495) (x496, 496) (x497, 497) (x498, 498) (x499, 499)
Level2: (x403, 403) (x405, 405) (x406, 406) (x408, 408) (x409, 409) (x410, 410) (x411, 411) (x413, 413) (x414, 414) (x419, 419) (x420, 420) (x423, 423) (x424, 424) (x425, 425) (x426, 426) (x427, 427) (x430, 430) (x431, 431) (x433, 433) (x434, 434) (x435, 435) (x436, 436) (x437, 437) (x438, 438) (x440, 440) (x443, 443) (x444, 444) (x447, 447) (x451, 451) (x455, 455) (x456, 456) (x461, 461) (x462, 462) (x464, 464) (x465, 465) (x466, 466) (x467, 467) (x468, 468) (x469, 469) (x470, 470) (x473, 473) (x475, 475) (x476, 476) (x477, 477) (x479, 479) (x480, 480) (x481, 481) (x482, 482) (x485, 485) (x488, 488) (x489, 489) (x494, 494) (x495, 495) (x496, 496) (x498, 498) (x499, 499)
Level3: (x403, 403) (x406, 406) (x408, 408) (x410, 410) (x411, 411) (x413, 413) (x423, 423) (x430, 430) (x433, 433) (x435, 435) (x436, 436) (x438, 438) (x444, 444) (x447, 447) (x456, 456) (x467, 467) (x470, 470) (x480, 480) (x482, 482) (x485, 485) (x488, 488) (x494, 494) (x496, 496) (x498, 498)
Level4: (x403, 403) (x408, 408) (x413, 413) (x423, 423) (x430, 430) (x436, 436) (x438, 438) (x444, 444) (x467, 467) (x470, 470) (x480, 480) (x482, 482) (x485, 485) (x498, 498)
Level5: (x408, 408) (x413, 413) (x423, 423) (x436, 436) (x480, 480) (x485, 485) (x498, 498)
Level6: (x408, 408) (x413, 413) (x436, 436)
[2026-3-9-19-12-50][RaftApplyMessageSendToWaitChan--> raftserver{1}] , Send Command --> Index:{203} , ClientId {1960237232}, RequestId {203}, Opreation {%v}, Key :{%v}, Value :{%v}
[2026-3-9-19-12-50][func -KvServer::PutAppend -kvserver{1}]WaitChanGetRaftApplyMessage<--Server 1 , get Command <-- Index:203 , ClientId �\`�, RequestId 203, Opreation ��v�, Key :��v�, Value :��v�
[2026-3-9-19-12-50][func-Start-rf{1}]  lastLogIndex:204,command:@�v�

[2026-3-9-19-12-50][Raft::applierTicker() - raft{1}]  m_lastApplied{203}   m_commitIndex{203}
[2026-3-9-19-12-50][Raft::applierTicker() - raft{1}]  m_lastApplied{203}   m_commitIndex{203}
576[1;35m leaderHearBeatTicker();函数实际睡眠时间为: 23.6486 毫秒[0m
[2026-3-9-19-12-50][func-Raft::doHeartBeat()-Leader: {1}] Leader的心跳定时器触发了且拿到mutex，开始发送AE

[2026-3-9-19-12-50][func-Raft::doHeartBeat()-Leader: {1}] Leader的心跳定时器触发了 index:{0}

[2026-3-9-19-12-50][func-Raft::doHeartBeat()-Leader: {1}] Leader的心跳定时器触发了 index:{2}

[2026-3-9-19-12-50][func-Raft::sendAppendEntries-raft{1}] leader 向节点{0}发送AE rpc开始 ， args->entries_size():{1}
577[1;35m leaderHearBeatTicker();函数设置睡眠时间为: 24.9958 毫秒[0m
[2026-3-9-19-12-50][func- Raft::applierTicker()-raft{2}] 向kvserver报告的applyMsgs长度为：{1}
[2026-3-9-19-12-50]---------------tmp-------------[func-KvServer::ReadRaftApplyCommandLoop()-kvserver{2}] 收到了下raft的消息
[2026-3-9-19-12-50][KvServer::GetCommandFromRaft-kvserver{2}] , Got Command --> Index:{202} , ClientId {��}, RequestId {202}, Opreation {`��t�}, Key :{���t�}, Value :{���t�}
[2026-3-9-19-12-50][func- Raft::applierTicker()-raft{2}] 向kvserver报告的applyMsgs长度为：{1}
[2026-3-9-19-12-50]---------------tmp-------------[func-KvServer::ReadRaftApplyCommandLoop()-kvserver{2}] 收到了下raft的消息
[2026-3-9-19-12-50][KvServer::GetCommandFromRaft-kvserver{2}] , Got Command --> Index:{203} , ClientId {@/l�}, RequestId {203}, Opreation {`��t�}, Key :{���t�}, Value :{���t�}
search_element-----------------
Not Found Key:x398
Successfully inserted key:x398, value:398

***** Skip List *****
Level0: (x398, 398) (x399, 399) (x400, 400) (x401, 401) (x402, 402) (x403, 403) (x404, 404) (x405, 405) (x406, 406) (x407, 407) (x408, 408) (x409, 409) (x410, 410) (x411, 411) (x412, 412) (x413, 413) (x414, 414) (x415, 415) (x416, 416) (x417, 417) (x418, 418) (x419, 419) (x420, 420) (x421, 421) (x422, 422) (x423, 423) (x424, 424) (x425, 425) (x426, 426) (x427, 427) (x428, 428) (x429, 429) (x430, 430) (x431, 431) (x432, 432) (x433, 433) (x434, 434) (x435, 435) (x436, 436) (x437, 437) (x438, 438) (x439, 439) (x440, 440) (x441, 441) (x442, 442) (x443, 443) (x444, 444) (x445, 445) (x446, 446) (x447, 447) (x448, 448) (x449, 449) (x450, 450) (x451, 451) (x452, 452) (x453, 453) (x454, 454) (x455, 455) (x456, 456) (x457, 457) (x458, 458) (x459, 459) (x460, 460) (x461, 461) (x462, 462) (x463, 463) (x464, 464) (x465, 465) (x466, 466) (x467, 467) (x468, 468) (x469, 469) (x470, 470) (x471, 471) (x472, 472) (x473, 473) (x474, 474) (x475, 475) (x476, 476) (x477, 477) (x478, 478) (x479, 479) (x480, 480) (x481, 481) (x482, 482) (x483, 483) (x484, 484) (x485, 485) (x486, 486) (x487, 487) (x488, 488) (x489, 489) (x490, 490) (x491, 491) (x492, 492) (x493, 493) (x494, 494) (x495, 495) (x496, 496) (x497, 497) (x498, 498) (x499, 499)
Level1: (x398, 398) (x399, 399) (x400, 400) (x401, 401) (x402, 402) (x403, 403) (x404, 404) (x405, 405) (x406, 406) (x407, 407) (x408, 408) (x409, 409) (x410, 410) (x411, 411) (x412, 412) (x413, 413) (x414, 414) (x415, 415) (x416, 416) (x417, 417) (x418, 418) (x419, 419) (x420, 420) (x421, 421) (x422, 422) (x423, 423) (x424, 424) (x425, 425) (x426, 426) (x427, 427) (x428, 428) (x429, 429) (x430, 430) (x431, 431) (x432, 432) (x433, 433) (x434, 434) (x435, 435) (x436, 436) (x437, 437) (x438, 438) (x439, 439) (x440, 440) (x441, 441) (x442, 442) (x443, 443) (x444, 444) (x445, 445) (x446, 446) (x447, 447) (x448, 448) (x449, 449) (x450, 450) (x451, 451) (x452, 452) (x453, 453) (x454, 454) (x455, 455) (x456, 456) (x457, 457) (x458, 458) (x459, 459) (x460, 460) (x461, 461) (x462, 462) (x463, 463) (x464, 464) (x465, 465) (x466, 466) (x467, 467) (x468, 468) (x469, 469) (x470, 470) (x471, 471) (x472, 472) (x473, 473) (x474, 474) (x475, 475) (x476, 476) (x477, 477) (x478, 478) (x479, 479) (x480, 480) (x481, 481) (x482, 482) (x483, 483) (x484, 484) (x485, 485) (x486, 486) (x487, 487) (x488, 488) (x489, 489) (x490, 490) (x491, 491) (x492, 492) (x493, 493) (x494, 494) (x495, 495) (x496, 496) (x497, 497) (x498, 498) (x499, 499)
Level2: (x403, 403) (x405, 405) (x406, 406) (x408, 408) (x409, 409) (x410, 410) (x411, 411) (x413, 413) (x414, 414) (x419, 419) (x420, 420) (x423, 423) (x424, 424) (x425, 425) (x426, 426) (x427, 427) (x430, 430) (x431, 431) (x433, 433) (x434, 434) (x435, 435) (x436, 436) (x437, 437) (x438, 438) (x440, 440) (x443, 443) (x444, 444) (x447, 447) (x451, 451) (x455, 455) (x456, 456) (x461, 461) (x462, 462) (x464, 464) (x465, 465) (x466, 466) (x467, 467) (x468, 468) (x469, 469) (x470, 470) (x473, 473) (x475, 475) (x476, 476) (x477, 477) (x479, 479) (x480, 480) (x481, 481) (x482, 482) (x485, 485) (x488, 488) (x489, 489) (x494, 494) (x495, 495) (x496, 496) (x498, 498) (x499, 499)
Level3: (x403, 403) (x406, 406) (x408, 408) (x410, 410) (x411, 411) (x413, 413) (x423, 423) (x430, 430) (x433, 433) (x435, 435) (x436, 436) (x438, 438) (x444, 444) (x447, 447) (x456, 456) (x467, 467) (x470, 470) (x480, 480) (x482, 482) (x485, 485) (x488, 488) (x494, 494) (x496, 496) (x498, 498)
Level4: (x403, 403) (x408, 408) (x413, 413) (x423, 423) (x430, 430) (x436, 436) (x438, 438) (x444, 444) (x467, 467) (x470, 470) (x480, 480) (x482, 482) (x485, 485) (x498, 498)
Level5: (x408, 408) (x413, 413) (x423, 423) (x436, 436) (x480, 480) (x485, 485) (x498, 498)
Level6: (x408, 408) (x413, 413) (x436, 436)
[2026-3-9-19-12-50][func- Raft::applierTicker()-raft{0}] 向kvserver报告的applyMsgs长度为：{1}
[2026-3-9-19-12-50]---------------tmp-------------[func-KvServer::ReadRaftApplyCommandLoop()-kvserver{0}] 收到了下raft的消息
[2026-3-9-19-12-50][KvServer::GetCommandFromRaft-kvserver{0}] , Got Command --> Index:{202} , ClientId {��}, RequestId {202}, Opreation {`�t�}, Key :{��t�}, Value :{��t�}
[2026-3-9-19-12-50][func- Raft::applierTicker()-raft{0}] 向kvserver报告的applyMsgs长度为：{1}
[2026-3-9-19-12-50]---------------tmp-------------[func-KvServer::ReadRaftApplyCommandLoop()-kvserver{0}] 收到了下raft的消息
[2026-3-9-19-12-50][KvServer::GetCommandFromRaft-kvserver{0}] , Got Command --> Index:{203} , ClientId {@/h�}, RequestId {203}, Opreation {`�t�}, Key :{��t�}, Value :{��t�}
search_element-----------------
Not Found Key:x398
Successfully inserted key:x398, value:398

***** Skip List *****
Level0: (x398, 398) (x399, 399) (x400, 400) (x401, 401) (x402, 402) (x403, 403) (x404, 404) (x405, 405) (x406, 406) (x407, 407) (x408, 408) (x409, 409) (x410, 410) (x411, 411) (x412, 412) (x413, 413) (x414, 414) (x415, 415) (x416, 416) (x417, 417) (x418, 418) (x419, 419) (x420, 420) (x421, 421) (x422, 422) (x423, 423) (x424, 424) (x425, 425) (x426, 426) (x427, 427) (x428, 428) (x429, 429) (x430, 430) (x431, 431) (x432, 432) (x433, 433) (x434, 434) (x435, 435) (x436, 436) (x437, 437) (x438, 438) (x439, 439) (x440, 440) (x441, 441) (x442, 442) (x443, 443) (x444, 444) (x445, 445) (x446, 446) (x447, 447) (x448, 448) (x449, 449) (x450, 450) (x451, 451) (x452, 452) (x453, 453) (x454, 454) (x455, 455) (x456, 456) (x457, 457) (x458, 458) (x459, 459) (x460, 460) (x461, 461) (x462, 462) (x463, 463) (x464, 464) (x465, 465) (x466, 466) (x467, 467) (x468, 468) (x469, 469) (x470, 470) (x471, 471) (x472, 472) (x473, 473) (x474, 474) (x475, 475) (x476, 476) (x477, 477) (x478, 478) (x479, 479) (x480, 480) (x481, 481) (x482, 482) (x483, 483) (x484, 484) (x485, 485) (x486, 486) (x487, 487) (x488, 488) (x489, 489) (x490, 490) (x491, 491) (x492, 492) (x493, 493) (x494, 494) (x495, 495) (x496, 496) (x497, 497) (x498, 498) (x499, 499)
Level1: (x398, 398) (x399, 399) (x400, 400) (x401, 401) (x402, 402) (x403, 403) (x404, 404) (x405, 405) (x406, 406) (x407, 407) (x408, 408) (x409, 409) (x410, 410) (x411, 411) (x412, 412) (x413, 413) (x414, 414) (x415, 415) (x416, 416) (x417, 417) (x418, 418) (x419, 419) (x420, 420) (x421, 421) (x422, 422) (x423, 423) (x424, 424) (x425, 425) (x426, 426) (x427, 427) (x428, 428) (x429, 429) (x430, 430) (x431, 431) (x432, 432) (x433, 433) (x434, 434) (x435, 435) (x436, 436) (x437, 437) (x438, 438) (x439, 439) (x440, 440) (x441, 441) (x442, 442) (x443, 443) (x444, 444) (x445, 445) (x446, 446) (x447, 447) (x448, 448) (x449, 449) (x450, 450) (x451, 451) (x452, 452) (x453, 453) (x454, 454) (x455, 455) (x456, 456) (x457, 457) (x458, 458) (x459, 459) (x460, 460) (x461, 461) (x462, 462) (x463, 463) (x464, 464) (x465, 465) (x466, 466) (x467, 467) (x468, 468) (x469, 469) (x470, 470) (x471, 471) (x472, 472) (x473, 473) (x474, 474) (x475, 475) (x476, 476) (x477, 477) (x478, 478) (x479, 479) (x480, 480) (x481, 481) (x482, 482) (x483, 483) (x484, 484) (x485, 485) (x486, 486) (x487, 487) (x488, 488) (x489, 489) (x490, 490) (x491, 491) (x492, 492) (x493, 493) (x494, 494) (x495, 495) (x496, 496) (x497, 497) (x498, 498) (x499, 499)
Level2: (x403, 403) (x405, 405) (x406, 406) (x408, 408) (x409, 409) (x410, 410) (x411, 411) (x413, 413) (x414, 414) (x419, 419) (x420, 420) (x423, 423) (x424, 424) (x425, 425) (x426, 426) (x427, 427) (x430, 430) (x431, 431) (x433, 433) (x434, 434) (x435, 435) (x436, 436) (x437, 437) (x438, 438) (x440, 440) (x443, 443) (x444, 444) (x447, 447) (x451, 451) (x455, 455) (x456, 456) (x461, 461) (x462, 462) (x464, 464) (x465, 465) (x466, 466) (x467, 467) (x468, 468) (x469, 469) (x470, 470) (x473, 473) (x475, 475) (x476, 476) (x477, 477) (x479, 479) (x480, 480) (x481, 481) (x482, 482) (x485, 485) (x488, 488) (x489, 489) (x494, 494) (x495, 495) (x496, 496) (x498, 498) (x499, 499)
Level3: (x403, 403) (x406, 406) (x408, 408) (x410, 410) (x411, 411) (x413, 413) (x423, 423) (x430, 430) (x433, 433) (x435, 435) (x436, 436) (x438, 438) (x444, 444) (x447, 447) (x456, 456) (x467, 467) (x470, 470) (x480, 480) (x482, 482) (x485, 485) (x488, 488) (x494, 494) (x496, 496) (x498, 498)
Level4: (x403, 403) (x408, 408) (x413, 413) (x423, 423) (x430, 430) (x436, 436) (x438, 438) (x444, 444) (x467, 467) (x470, 470) (x480, 480) (x482, 482) (x485, 485) (x498, 498)
Level5: (x408, 408) (x413, 413) (x423, 423) (x436, 436) (x480, 480) (x485, 485) (x498, 498)
Level6: (x408, 408) (x413, 413) (x436, 436)
[2026-3-9-19-12-50][func-Raft::sendAppendEntries-raft{1}] leader 向节点{2}发送AE rpc开始 ， args->entries_size():{1}
[2026-3-9-19-12-50][func-Raft::sendAppendEntries-raft{1}] leader 向节点{0}发送AE rpc成功
[2026-3-9-19-12-50]---------------------------tmp------------------------- 节点{0}返回true,当前*appendNums{2}
[2026-3-9-19-12-50]args->entries(args->entries_size()-1).logterm(){1}   currentTerm{1}
[2026-3-9-19-12-50]---------------------------tmp------------------------- 當前term有log成功提交，更新leader的m_commitIndex from{203} to{204}
[2026-3-9-19-12-50][func-Raft::sendAppendEntries-raft{1}] leader 向节点{2}发送AE rpc成功
[2026-3-9-19-12-50]---------------------------tmp------------------------- 节点{2}返回true,当前*appendNums{1}
[2026-3-9-19-12-50][Raft::applierTicker() - raft{1}]  m_lastApplied{204}   m_commitIndex{204}
[2026-3-9-19-12-50][func- Raft::applierTicker()-raft{1}] 向kvserver报告的applyMsgs长度为：{1}
[2026-3-9-19-12-50]---------------tmp-------------[func-KvServer::ReadRaftApplyCommandLoop()-kvserver{1}] 收到了下raft的消息
[2026-3-9-19-12-50][KvServer::GetCommandFromRaft-kvserver{1}] , Got Command --> Index:{204} , ClientId {Z`�}, RequestId {204}, Opreation {`��t�}, Key :{���t�}, Value :{���t�}
[2026-3-9-19-12-50][RaftApplyMessageSendToWaitChan--> raftserver{1}] , Send Command --> Index:{204} , ClientId {1960237232}, RequestId {204}, Opreation {%v}, Key :{%v}, Value :{%v}
search_element-----------------
Found key: x398, value: 398

```

以下是详细的阶段分析：

### 1. 第一阶段：Leader 发起日志同步 (Index 203)

- 
- **心跳触发**：Leader (raft{1}) 的定时器唤醒，准备发送 AppendEntries (AE)。
- **携带数据**：args->entries_size():{1} 此时 Leader 发现有一条新日志（Index 203）需要同步。
- **网络发送**：Leader 向节点 {0} 和 {2} 发送 RPC。

### 2. 第二阶段：达成共识 (Consensus)

- 
- **多数派确认**：节点 {0} 返回成功。此时 *appendNums 变为 **2**。在一个 3 节点的集群中，2 已经超过了半数（3/2 + 1 = 2）。
- **提交日志**：因为达到了大多数，Leader 运行安全性检查：logterm{1} == currentTerm{1}。
- **更新 CommitIndex**：Leader 将 m_commitIndex 从 **202 推进到 203**。

### 3. 第三阶段：状态机应用 (Apply)

- 
- **Applier 监测**：applierTicker 发现 commitIndex(203) > lastApplied(202)。
- **投递消息**：将 Index 203 包装成 ApplyMsg 丢进 applyChan。
- **KVServer 响应**：KvServer 的后台循环 ReadRaftApplyCommandLoop 弹出这条消息。
- **执行业务逻辑**：GetCommandFromRaft 识别出这是一条 Put 或 Append 指令。
  - 
  - **查询跳表**：search_element -> Not Found Key:x398。
  - **写入数据**：Successfully inserted key:x398, value:398。

### 4. 第四阶段：存储引擎可视化 (Skip List)

- 
- 日志打印了当前的 **跳表结构**。
- 可以看到数据分布在不同的层级（Level 0 到 Level 6），这说明你的 getRandomLevel 随机层数逻辑工作正常。
- 所有的 Key（x398, x399...）都按顺序排列在 Level 0。

### 5. 第五阶段：唤醒客户端 RPC 线程 (WaitChan)

- 
- **通知发送**：SendMessageToWaitChan 被调用。它找到了正在为 Index 203 阻塞等待的那个 RPC 线程。
- **解除阻塞**：WaitChanGetRaftApplyMessage 打印。此时，原本调用 PutAppend RPC 的函数终于拿到了结果。
- **校验与返回**：RPC 线程被唤醒，校验 RequestId (203)，并给客户端回信。

------

终端输出：

```bash
tr@tr-virtual-machine:~/文档/KVstorage/KVstorage/bin$ ./callerMain
[2026-3-9-19-12-45][func-MprpcChannel::CallMethod]连接ip：{127.0.1.1} port{13400}成功
[2026-3-9-19-12-45][func-MprpcChannel::CallMethod]连接ip：{127.0.1.1} port{13401}成功
Get x499: 499
Get x498: 498
Get x497: 497
Get x496: 496
……
Get x8: 8
Get x7: 7
Get x6: 6
Get x5: 5
Get x4: 4
Get x3: 3
Get x2: 2
Get x1: 1
Get x0: 0
```



