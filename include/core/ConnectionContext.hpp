#pragma once

#include <memory>
#include <string>

class ClientConnection;
class TimerManager;
class ThreadPool;
class EpollLoop;
class LLMService;

// 引入上下文传递，共享基础设备，不再层层转接指针
struct ConnectionContext
{
    std::weak_ptr<ClientConnection> conn; // 用于发送数据、唤醒主循环,使用弱指针，确保生命周期和内存安全
    ThreadPool &thread_pool;              // 提交异步任务
    TimerManager &timer_manager;          // 定时器操作
    EpollLoop &loop;                      // 主循环唤醒
    LLMService *llm_service;              // 大模型服务

    // 临时存储 /v1/chat 请求数据，用于升级后的 SseHandler 使用
    struct PendingLLMRequest
    {
        std::string session_id;
        std::string message;
        bool valid = false;
    };
    mutable PendingLLMRequest pending_llm;

    ConnectionContext(std::weak_ptr<ClientConnection> c,
                      ThreadPool &tp,
                      TimerManager &tm, EpollLoop &l,
                      LLMService &ls) : conn(c), thread_pool(tp), timer_manager(tm), loop(l), llm_service(&ls) {};

    // 获取共享指针
    std::shared_ptr<ClientConnection> get_conn() const
    {
        return conn.lock();
    }

    // 检查连接生命
    bool is_conn_alive() const
    {
        return !conn.expired();
    }

    // 便捷发送，内部处理空检查
    // 返回 true 表示发送成功，false 表示连接已断开
    bool send_data(const std::string &data) const
    {
        if (auto c = conn.lock())
        {
            c->send_data(data);
            return true;
        }
        else
        {
            return false;
        }
    }
};