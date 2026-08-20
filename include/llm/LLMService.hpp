#pragma once

#include <string>
#include <memory>
#include "llm/SessionManager.hpp"
#include "llm/HttpClient.hpp"
#include "core/ConnectionContext.hpp"
#include "tools/ToolExecutor.hpp"

/**
 * @brief LLM 服务层：网关与 LLM 之间的“内部网关”
 *
 * 职责：
 *   - 接受 HttpChannel 转发的 /v1/chat 请求
 *   - 调用 SessionManager 管理会话上下文
 *   - 通过 HttpClient 调用本地或远程 LLM 服务
 *   - 将流式响应通过 SseHandler 推送给客户端
 *   - 处理错误、超时、回退等异常情况
 */

class LLMService
{
public:
    /**
     * @brief 构造函数
     * @param session_manager 会话管理器引用 （由上层持有）
     */

    explicit LLMService(SessionManager &session_manager, ToolRegistry &tool_registry);

    /**
     * @brief 处理聊天请求 （主入口）
     *
     * @param session_id 会话 ID
     * @param message    用户消息
     * @param ctx        连接上下文（用于访问 Ssehandler 和写缓冲区）
     *
     * 流程：
     *   1. 获取历史消息（SessionManager::get_history）
     *   2. 构造请求体 (历史 + 当前消息)
     *   3. 调用 HttpClient 向 llama.cpp 发起流式请求
     *   4. 每收到一个数据块，通过 SseHandler 推送给客户端
     *   5. 响应完成后，更新会话历史
     */

    void handle_chat(
        const std::string &session_id,
        const std::string &message,
        const ConnectionContext &ctx);

private:
    // 辅助构造请求体 JSON
    std::string build_llm_request(
        const std::vector<ChatMessage> &history,
        const std::string &message);

    // 辅助处理流式数据块
    void process_stream_chunk(
        const std::string &chunk,
        const ConnectionContext &ctx,
        std::string &accumulated_response);

    // 按行处理数据，辅助解决粘包/分包问题
    void proecess_sse_line(
        const std::string &line,
        const ConnectionContext &ctx,
        std::string &accumulated);

    // 发送错误时间到客户端
    void send_error_event(const ConnectionContext &ctx, const std::string &error_msg);

    // 依赖
    SessionManager &session_manager_;
    HttpClient http_client_;

    // 工具调用成员
    ToolRegistry &tool_registry_;
    ToolExecutor tool_executor_;
};