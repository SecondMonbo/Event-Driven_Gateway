#include "llm/LLMService.hpp"
#include "third_party/json.hpp" //nlohmann/json
#include <iostream>
#include <sstream>
#include "core/ClientConnection.hpp"

using json = nlohmann::json;

LLMService::LLMService(SessionManager &session_manager, ToolRegistry &tool_regitry) : session_manager_(session_manager), tool_registry_(tool_regitry), tool_executor_(tool_regitry) {}

// 辅助函数

std::string LLMService::build_llm_request(
    const std::vector<ChatMessage> &history,
    const std::string &message)
{
    json request;
    request["model"] = "Phi-3";
    request["stream"] = true;

    // 构建 message 数组
    json messages = json::array();

    // 添加历史消息
    for (const auto &msg : history)
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

std::string LLMService::build_llm_request_with_tools(
    const std::vector<ChatMessage> &history,
    const std::string &message,
    bool enable_tools)
{
    json request;
    request["model"] = "Phi-3";
    request["stream"] = true;

    // 构建 message 数组
    json messages = json::array();

    // 添加历史消息
    for (const auto &msg : history)
    {
        messages.push_back({{"role", msg.role},
                            {"content", msg.content}});
    }

    // 添加当前用户消息
    messages.push_back({{"role", "user"},
                        {"content", message}});

    request["messages"] = messages;

    // 启用工具
    if (enable_tools)
    {
        json tools = json::array();
        auto defs = tool_registry_.get_all_definitions();
        for (const auto &def : defs)
        {
            json tool_def = {
                {"type", "function"},
                {"function",
                 {{"name", def.name},
                  {"description", def.description},
                  {"parameters", def.parameters}}}};
            tools.push_back(tool_def);
        }
        if (!tools.empty())
        {
            request["tools"] = tools;
            request["tool_choice"] = "auto";
        }
    }
    return request.dump();
}

void LLMService::handle_chat_with_tools(
    const std::string &session_id,
    const std::string &message,
    const ConnectionContext &ctx,
    int max_iterations)
{
    if (use_deepseek_)
    {
        call_deepseek_api(session_id, message, ctx, true);
    }

    auto history = session_manager_.get_history(session_id);
    std::string current_message = message;
    std::string full_response;
    int iteration = 0;

    while (iteration < max_iterations)
    {
        iteration++;
        bool enable_tools = (iteration == 1); // 目前仅支持第一轮启用工具，后续不启用
        std::string request_body = build_llm_request_with_tools(history, current_message, enable_tools);

        // 存储本次请求响应
        std::string accumulated;
        bool has_tool_calls = false;
        std::vector<ToolCall> pending_calls;

        // 发起 HTTP 请求
        http_client_.post(
            "http://localhost:8081/v1/chat/completions",
            request_body,
            // on_data: 处理流式数据
            [this, &ctx, &accumulated, &has_tool_calls, &pending_calls, &session_id](const char *data, size_t len)
            {
                std::string chunk(data, len);
                // 检查是否有 tool_calls
                if (chunk.find("tool_calls") != std::string::npos)
                {
                    // 解析并执行工具
                    try
                    {
                        // 只解析包含 tool_calls 的 chunk
                        json data_json = json::parse(chunk);
                        auto results = tool_executor_.execute_from_json(data_json);
                        if (!results.empty())
                        {
                            has_tool_calls = true;
                            // 结果保存至会话历史
                            for (const auto &result : results)
                            {
                                // 将工具结果作为 assistant 消息
                                session_manager_.append_message(session_id,
                                                                ChatMessage("assistant", "调用工具: " + result.result));
                                // 将工具结果作为 tool 消息
                                session_manager_.append_message(session_id, ChatMessage("tool", result.result));

                                // 2. 推送给客户端（SSE 格式）
                                std::string event = "data: [TOOL_RESULT] " + result.result + "\n\n";
                                ctx.conn->send_data(event);
                            }
                        }
                    }
                    catch (const std::exception &e)
                    {
                        std::cerr << "Tool execution error: " << e.what() << std::endl;
                    }
                }
                else
                {
                    // 正常处理 content
                    process_sse_line(chunk, ctx, accumulated);
                }
            },
            // on complete
            [this, &session_id, &message, &accumulated, &has_tool_calls, &current_message, &history, &ctx, &iteration, max_iterations](bool sucess)
            {
                if (sucess)
                {
                    if (!has_tool_calls)
                    {
                        // 没有工具调用，保存最终回答
                        session_manager_.append_message(session_id, ChatMessage("user", message));
                        session_manager_.append_message(session_id, ChatMessage("assitant", accumulated));
                    }
                    else
                    {
                        // 有工具调用，需要继续循环
                        // 更新 current_message 为空，表示继续处理
                        current_message = "";
                    }
                }
            });

        if (!has_tool_calls)
        {
            break;
        }
    }
}

void LLMService::call_deepseek_api(
    const std::string &session_id,
    const std::string &message,
    const ConnectionContext &ctx,
    bool enable_tools)
{
    auto history = session_manager_.get_history(session_id);

    // 构建请求体
    json request_body;
    request_body["model"] = "deepseek-v4-pro";
    request_body["stream"] = true;

    json messages = json::array();
    for (const auto &msg : history)
    {
        messages.push_back({{"role", msg.role}, {"content", msg.content}});
    }
    messages.push_back({{"role", "user"}, {"content", message}});
    request_body["messages"] = messages;

    if (enable_tools)
    {
        json tools = json::array();
        auto defs = tool_registry_.get_all_definitions();
        for (const auto &def : defs)
        {
            tools.push_back({{"type", "function"},
                             {"function", {{"name", def.name}, {"description", def.description}, {"parameters", def.parameters}}}});
        }
        if (!tools.empty())
        {
            request_body["tools"] = tools;
            request_body["tool_choice"] = "auto";
        }
    }

    // 设置认证头
    std::unordered_map<std::string, std::string> headers;
    const char *api_key = std::getenv("DEEPSEEK_API_KEY");
    if (!api_key)
    {
        std::cerr << "Warning: DEEPSEEK_API_KEY not set" << std::endl;
        return;
    }
    headers["Authorization"] = "Bearer " + std::string(api_key);

    std::cout << "=== Request Body ===" << std::endl;
    std::cout << request_body.dump(2) << std::endl;

    //  在作用域中定义 has_tool_calls
    bool has_tool_calls = false;
    std::string accumulated;

    http_client_.post(
        "https://api.deepseek.com/v1/chat/completions",
        request_body.dump(),
        // on_data
        [this, &ctx, &accumulated, &session_id, &has_tool_calls](const char *data, size_t len)
        {
            std::string raw(data, len);
            std::istringstream stream(raw);
            std::string line;
            while (std::getline(stream, line))
            {
                if (line.find("tool_calls") != std::string::npos)
                {
                    try
                    {
                        const std::string prefix = "data: ";
                        std::string json_str;
                        if (line.compare(0, prefix.size(), prefix) == 0)
                        {
                            json_str = line.substr(prefix.size());
                            while (!json_str.empty() && (json_str.back() == '\n' || json_str.back() == '\r' || json_str.back() == ' '))
                            {
                                json_str.pop_back();
                            }
                        }
                        else
                        {
                            return;
                        }

                        json data_json = json::parse(json_str);
                        auto results = tool_executor_.execute_from_json(data_json);
                        if (!results.empty())
                        {
                            for (const auto &result : results)
                            {
                                // 推送给客户端
                                std::string event = "data: " + result.result + "\n\n";
                                ctx.conn->send_data(event);

                                // 保存到历史
                                session_manager_.append_message(session_id, ChatMessage("assistant", "调用工具: " + result.result));
                                session_manager_.append_message(session_id, ChatMessage("tool", result.result));

                                // 设置标志
                                has_tool_calls = true;
                            }
                        }
                    }
                    catch (const std::exception &e)
                    {
                        std::cerr << "Tool execution error: " << e.what() << std::endl;
                        ctx.conn->send_data("data: {\"error\": \"Tool execution failed\"}\n\n");
                    }
                }
                else
                {
                    // 普通 content，交给 process_sse_line
                    process_sse_line(line, ctx, accumulated);
                }
            }
        },
        // on_complete
        [this, &session_id, &message, &accumulated, &has_tool_calls, &ctx, enable_tools](bool success)
        {
            if (success)
            {
                // 如果没有工具调用，保存最终回答
                if (!has_tool_calls)
                {
                    session_manager_.append_message(session_id, ChatMessage("user", message));
                    session_manager_.append_message(session_id, ChatMessage("assistant", accumulated));
                }
                else
                {
                    // 有工具调用，发起第二轮请求（不带工具）
                    // 注意：第二轮不传入新消息，因为历史中已包含工具结果
                    call_deepseek_api(session_id, "", ctx, false);
                }
            }
            else
            {
                std::cerr << "DeepSeek API call failed" << std::endl;
            }
        },
        60,
        headers);
}

void LLMService::process_stream_chunk(
    const std::string &chunk,
    const ConnectionContext &ctx,
    std::string &accumulated)
{
    std::istringstream stream(chunk);
    std::string line;
    while (std::getline(stream, line))
    {
        // 去除末尾 \r （windows 风格换行）
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        process_sse_line(line, ctx, accumulated);
    }
}

void LLMService::process_sse_line(
    const std::string &line,
    const ConnectionContext &ctx,
    std::string &accumulated)
{

    if (line.empty())
        return;

    const std::string prefix = "data: ";
    if (line.compare(0, prefix.size(), prefix) != 0)
    {
        return;
    }

    std::string json_str = line.substr(prefix.size());

    // 去除空白字符
    while (!json_str.empty() && (json_str.back() == '\n' || json_str.back() == '\r' || json_str.back() == ' '))
    {
        json_str.pop_back();
    }

    // 检查[Done]
    if (json_str == "[DONE]")
    {
        std::cout << "Recived [DONE], finishind stream" << std::endl;
        return;
    }

    if (!json_str.empty() && json_str[0] != '{')
    {
        // 直接发送原始数据（保留 "data: " 前缀）
        ctx.conn->send_data(line + "\n\n");
        return;
    }

    try
    {
        json data = json::parse(json_str);
        if (data.contains("choices") && data["choices"].is_array() && !data["choices"].empty())
        {
            auto &choice = data["choices"][0];
            if (choice.contains("delta"))
            {
                auto &delta = choice["delta"];

                //  1. 提取内容（兼容 content 和 reasoning_content）
                std::string content;
                if (delta.contains("content") && !delta["content"].is_null())
                {
                    content = delta["content"].get<std::string>();
                }
                else if (delta.contains("reasoning_content") && !delta["reasoning_content"].is_null())
                {
                    content = delta["reasoning_content"].get<std::string>();
                }

                if (!content.empty())
                {
                    accumulated += content;
                    ctx.conn->send_data("data: " + content + "\n\n");
                }

                //  2. 检测 tool_calls（但不在 process_sse_line 中执行）
                // 这里只记录日志或忽略，因为工具调用由 call_deepseek_api 的 on_data 处理
                if (delta.contains("tool_calls") && !delta["tool_calls"].is_null())
                {
                    // 可选：打印日志，表明收到了 tool_calls
                    std::cout << "Received tool_calls in process_sse_line (will be handled in on_data)" << std::endl;
                    // 不要在这里执行工具，因为执行逻辑在 on_data 中更完整
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "JSON parse error" << e.what() << std::endl;
        std::cerr << "Failed json_str: " << json_str << std::endl;
        std::cerr.flush();
        // 确认只发送一次错误事件
        // static bool error_sent = false;
        // if (!error_sent)
        // {
        //     send_error_event(ctx, "Failed to parse LLM reponse");
        //     error_sent = true;
        // }
    }
}

void LLMService::send_error_event(const ConnectionContext &ctx, const std::string &error_msg)
{
    std::string event = "data: {\"error\":\"" + error_msg + "\"}\n\n";
    ctx.conn->send_data(event);
}