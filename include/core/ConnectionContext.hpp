#pragma once

#include <memory>
#include <string>
#include <functional>

class ClientConnection;
class TimerManager;
class ThreadPool;
class EpollLoop;
class LLMService;

/**
 * @brief 连接上下文
 *
 * 职责边界：
 *    放入“与一个连接”生命周期直接绑定的资源和状态
 *    应用全局服务不包含，具体使用的业务将由更高层回调注入
 */

struct ConnectionContext
{
    std::weak_ptr<ClientConnection> conn; // 用于发送数据、唤醒主循环,使用弱指针，确保生命周期和内存安全
    ThreadPool &thread_pool;              // 提交异步任务
    TimerManager &timer_manager;          // 定时器操作
    EpollLoop &loop;                      // 主循环唤醒

    // 临时存储 /v1/chat 请求数据，用于升级后的 SseHandler 使用
    struct PendingLLMRequest
    {
        std::string session_id;
        std::string message;
        bool valid = false;
    };
    mutable PendingLLMRequest pending_llm;

    // 业务能力注入：
    // 由上层(EpollLoop)注入：收到对话请求时，如何执行
    // Context 不负责具体执行
    using ChatHandler = std::function<void(const std::string &session_id,
                                           const std::string &message, const ConnectionContext &ctx)>;
    ChatHandler chat_handler;

    ConnectionContext(std::weak_ptr<ClientConnection> c,
                      ThreadPool &tp,
                      TimerManager &tm, EpollLoop &l);

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
    bool send_data(const std::string &data) const;
};