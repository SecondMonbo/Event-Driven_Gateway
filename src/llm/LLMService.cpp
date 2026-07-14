#include "llm/LLMService.hpp"
#include "third_party/json.hpp" //nlohmann/json
#include <iostream>
#include <sstream>
#include "core/ClientConnection.hpp"

using json = nlohmann::json;

LLMService::LLMService(SessionManager &session_manager) : session_manager_(session_manager) {}

// 辅助函数

std::string LLMService::build_llm_request(
    const std::vector<ChatMessage> &histroy,
    const std::string &message)
{
    json request;
    request["model"] = "Phi-3";
    request["stream"] = true;

    // 构建 message 数组
    json messages = json::array();

    // 添加历史消息
    for (const auto &msg : histroy)
    {
        messages.push_back({{"role", msg.role},
                            {"content", msg.content}});
    }

    // 添加当前用户消息
    messages.push_back({{"role", "user"},
                        {"content", message}});

    request["messages"] = messages;
    return request.dump(); // 转换为 JSON 字符串
}

void LLMService::handle_chat(
    const std::string &session_id,
    const std::string &message,
    const ConnectionContext &ctx)
{
    // 1. 获取历史消息
    auto history = session_manager_.get_history(session_id);

    // 2. 构造请求体
    std::string request_body = build_llm_request(history, message);

    // 3. 准备累加响应 (用于最终保存)
    std::string full_response;

    // 4. 发起 HTTP 请求 (流式)
    http_client_.post(
        "http://localhost:8081/v1/chat/completions",
        request_body,
        // on_data: 每收到一个数据块
        [this, &ctx, &full_response](const char *data, size_t len)
        {
            std::string chunk(data, len);
            process_stream_chunk(chunk, ctx, full_response);
        },
        // on_complete: 请求完成
        [this, &session_id, &message, &full_response](bool success)
        {
            if (success)
            {
                // 保存用户信息和助手回复
                session_manager_.append_message(session_id, ChatMessage("user", message));
                session_manager_.append_message(session_id, ChatMessage("assistant", full_response));
            }
            else
            {
                // 失败时打印日志，发送错误事件（在process_stream_chunk中处理）
                std::cerr << "LLM request failed for session: " << session_id << std::endl;
            }
        });
}

void LLMService::process_stream_chunk(
    const std::string &chunk,
    const ConnectionContext &ctx,
    std::string &accumulated)
{
    // 收到[done]则忽略
    if (chunk.find("[DONE]") != std::string::npos)
        return;

    // 解析 data: {...} 格式
    // eg: data: {"choices":[{"delta":{"content":"你好"}}]}\n\n
    const std::string prefix = "data: ";
    if (chunk.compare(0, prefix.size(), prefix) != 0)
    {
        // 不是 data 行则忽略
        return;
    }

    // 提取 JSON 部分
    std::string json_str = chunk.substr(prefix.size());
    // 去除末尾的换行与空格
    while (!json_str.empty() && (json_str.back() == '\n' || json_str.back() == '\r' || json_str.back() == ' '))
    {
        json_str.pop_back();
    }

    try
    {
        json data = json::parse(json_str);
        // 提取content
        if (data.contains("choices") && data["choices"].is_array() && !data["choices"].empty())
        {
            auto &choice = data["choices"][0];
            if (choice.contains("delta") && choice["delta"].contains("content"))
            {
                std::string content = choice["delta"]["content"];
                if (!content.empty())
                {
                    // 累计完整响应
                    accumulated += content;
                    // 推送 SSE 事件
                    ctx.conn->send_data("data: " + content + "\n\n");
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        send_error_event(ctx, "Failed to parse LLM reponse");
    }
}

void LLMService::send_error_event(const ConnectionContext &ctx, const std::string &error_msg)
{
    std::string event = "data: {\"error\":\"" + error_msg + "\"}\n\n";
    ctx.conn->send_data(event);
}