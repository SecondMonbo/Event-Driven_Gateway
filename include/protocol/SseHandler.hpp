#pragma once

/*
SSE（Server-Sent Events，服务器推送事件） 是一种基于 HTTP 的轻量级实时通信协议，允许服务器主动向客户端推送数据。
特点：
1.服务器单向推送；
2.基于 HTTP/1.1 长连接；
3.自动重连。
*/

#include "protocol/ProtocolHandler.hpp"
#include <string>
#include <queue>
#include <mutex>
#include "core/ClientConnection.hpp"

class ThreadPool;

class SseHandler : public ProtocolHandler
{
public:
    explicit SseHandler(const ConnectionContext &ctx);
    ~SseHandler() override;

    // 统一接口
    ProcessResult process(std::string &read_buffer) override;
    void reset() override;

    // 外部调用:推送数据到SSE连接
    void push_event(const std::string &data, const std::string &event_type = "", const std::string &id = "");

    // 关闭连接
    void close();

    // 错误事件，直接发送
    void send_error_event(const std::string &error_msg);

    // 用以测试的sse协议心跳计时器(3秒)
    void start_heartbeat();

    void send_event(const std::string &data, const std::string &event_type, const std::string &id);

private:
    ClientConnection *const conn_;
    const ConnectionContext &ctx_;
    bool handshake_sent_ = false;
    bool closed_ = false;
    uint64_t timer_id_ = 0; // 定时器 ID, 0 表示无效

    // 待发送事件队列
    std::queue<std::string> pending_events_;
    std::mutex queue_mutex_;

    // 辅助函数：生成SSE格式的字符串
    std::string format_sse_message(const std::string &data, const std::string &event_type, const std::string &id);
};
