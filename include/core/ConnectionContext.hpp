#pragma once

class ClientConnection;
class TimerManager;
class ThreadPool;
class EpollLoop;

// 引入上下文传递，共享基础设备，不再层层转接指针
struct ConnectionContext
{
    ClientConnection *conn;      // 用于发送数据、唤醒主循环
    ThreadPool &thread_pool;     // 提交异步任务
    TimerManager &timer_manager; // 定时器操作
    EpollLoop &loop;             // 主循环唤醒

    ConnectionContext(ClientConnection *c, ThreadPool &tp, TimerManager &tm, EpollLoop &l) : conn(c), thread_pool(tp), timer_manager(tm), loop(l) {}
};