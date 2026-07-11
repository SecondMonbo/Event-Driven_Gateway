#pragma once

/*
SessionManager:会话管理器
职责：管理每个用户会话的完整上下文（对话历史、状态、元数据），为大模型推理提供“记忆”支持
主要功能：会话创建、消息存储、上下文检索、上下文截断、过期清理、并发安全
*/

#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

// 用于LLM对话的消息结构
struct ChatMessage
{
    std::string role;    //"user","assistant","system"
    std::string content; // 消息内容

    ChatMessage() = default;
    ChatMessage(const std::string &r, const std::string &c) : role(r), content(c) {};
};

class SeesionManager
{
public:
    // 获取会话历史消息
    std::vector<ChatMessage> get_history(const std::string &session_id);

    // 追加一条消息到会话中
    void append_message(const std::string &session_id, const ChatMessage &msg);

    // 获取最近 N 条消息(用于截断)
    std::vector<ChatMessage> get_current_messages(const std::string &session_id, int n);

    // 清理长时间未活动的会话（可定时调用）
    void cleanup_expired_sessions();

    // 设置对话最大长度
    void set_max_history(int max_messages);

private:
    std::unordered_map<std::string, std::vector<ChatMessage>> sessions_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_activity_;
    std::mutex mutex_;
    int max_history = 100;        // 默认最多保存100条消息
    int max_idle_seconds_ = 3600; // 默认最多保存1小时
};