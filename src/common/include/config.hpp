#pragma once

const bool Debug = true;

const int TimeUnit = 1; // 时间单位：time.Millisecond，不同网络环境rpc速度不同，因此需要乘以一个系数
const int HeartBeatTimeout = 25 * TimeUnit; // 心跳超时时间
const int ApplyInterval = 10 * TimeUnit; // 申请投票的时间间隔

const int minRandomizedElectionTimeout = 300 * TimeUnit; // 最小随机选举超时时间(ms)
const int maxRandomizedElectionTimeout = 500 * TimeUnit; // 最大随机选举超时时间(ms)

const int ConsensusTimeout = 500 * TimeUnit; // 共识超时时间(ms)

// 协程相关的配置

const int FIBER_THREAD_NUM = 1; // 协程线程数
const bool FIBER_USE_CALLER_THREAD = false; // 是否使用caller线程作为协程线程
