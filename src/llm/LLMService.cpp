#include "llm/LLMService.hpp"
#include "third_party/json.hpp" //nlohmann/json
#include "core/ClientConnection.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstdlib>

using json = nlohmann::json;

LLMService::LLMService(SessionManager &session_manager,
                       ToolRegistry &tool_regitry) : session_manager_(session_manager),
                                                     tool_registry_(tool_regitry) {}

// 主入口: 单次调用
LLMCallResult LLMService::call(const std::string &session_id, const std::string &message,
                               bool enable_tools, OutputCallback emit, const std::atomic<bool> *canceled)
{
    if (canceled && canceled->load())
    {
        LLMCallResult r;
        r.success = false;
        r.canceled = true;
        return r;
    }

    // 目前仅支持本地 llama 和远端 deepseel_api,后期扩展可以抽象改为注入
    if (backend_ == ModelBackend::DEEPSEEK_API)
    {
        return call_deepseek(session_id, message, enable_tools, emit);
    }
    else
    {
        return call_llama(session_id, message, enable_tools, emit);
    }
}

std::string LLMService::build_request_body(const std::string &session_id, const std::string &message, bool enable_tools)
{
    json request;
    request["model"] = (backend_ == ModelBackend::DEEPSEEK_API) ? "deepseek-v4-pro" : "Phi-3";
    request["stream"] = true;

    // 构造 messages
    json messages = json::array();
    auto history = session_manager_.get_history(session_id);
    for (const auto &msg : history)
    {
        json m;
        m["role"] = msg.role;

        if (msg.role == "assistant")
        {
            if (!msg.tool_calls.empty())
            {
                m["content"] = nullptr;
                json tcs = json::array();
                for (const auto &tc : msg.tool_calls)
                {
                    tcs.push_back({{"id", tc.id}, {"type", "function"}, {"function", {{"name", tc.name}, {"arguments", tc.arguments}}}});
                }
                m["tool_calls"] = tcs;
            }
            else
            {
                m["content"] = msg.content;
            }

            // DeepSeek V4 要求：如果模型返回 reasoning_content, 需要回传
            if (!msg.reasoning_content.empty())
            {
                m["reasoning_content"] = msg.reasoning_content;
            }
        }
        else if (msg.role == "tool")
        {
            m["content"] = msg.content;
            m["tool_call_id"] = msg.tool_call_id; // 携带以关联对应的 assistant 调用
        }
        else
        {
            m["content"] = msg.content;
        }

        messages.push_back(std::move(m));
    }
    // 追加当前用户消息 (后续轮次 message 为空)
    if (!message.empty())
    {
        messages.push_back({{"role", "user"},
                            {"content", message}});
    }
    request["messages"] = messages;

    // 附加工具定义
    if (enable_tools)
    {
        json tools = json::array();
        for (const auto &def : tool_registry_.get_all_definitions())
        {
            tools.push_back({{"type", "function"},
                             {"function", {{"name", def.name}, {"description", def.description}, {"parameters", def.parameters}}}});
        }
        if (!tools.empty())
        {
            request["tools"] = tools;
            request["tool_choice"] = "auto";
        }
    }
    return request.dump();
}
// DeepSeek 调用
LLMCallResult LLMService::call_deepseek(const std::string &session_id, const std::string &message,
                                        bool enable_tools, OutputCallback &emit, const std::atomic<bool> *canceled)
{
    LLMCallResult result;
    std::string body = build_request_body(session_id, message, enable_tools);

    // 认证头
    std::unordered_map<std::string, std::string> headers;
    const char *api_key = std::getenv("DEEPSEEK_API_KEY");
    if (!api_key)
    {
        std::cerr << "[LLMService] DEEPSEEK_API_KEY not set" << std::endl;
        result.success = false;
        result.error = "DEEPSEEK_API_KEY not set";
        return result;
    }
    headers["Authorization"] = "Bearer " + std::string(api_key);

    // 流式工具调用累计器
    std::unordered_map<int, ToolCallAccumulator> tool_call_acc;

    std::string pending_line;
    bool ok = http_client_.post(
        "https://api.deepseek.com/v1/chat/completions",
        body,
        // on_data: 按行拆分，逐行解析
        [this, &result, &emit, &tool_call_acc, &pending_line](const char *data, size_t len)
        {
            pending_line.append(data, len);

            size_t start = 0;
            size_t pos;

            while ((pos = pending_line.find('\n', start)) != std::string::npos)
            {
                std::string line = pending_line.substr(start, pos - start);
                start = pos + 1;

                // 处理 CRLF: 把行尾的 '\r' 去掉
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();

                process_sse_line(line, result, emit, tool_call_acc);
            }

            // 保留未处理的碎片
            if (start > 0)
            {
                pending_line.erase(0, start);
            }
        },
        // on_complete
        [&result](bool success)
        {
            result.success = success;
        },
        60, // timeout
        headers,
        canceled);

    if (canceled && canceled->load())
    {
        result.success = false;
        result.canceled = true;
    }

    // 稳妥处理最后一块碎片
    if (!pending_line.empty())
    {
        if (pending_line.back() == '\r')
            pending_line.pop_back();

        process_sse_line(pending_line, result, emit, tool_call_acc);
        pending_line.clear();
    }

    if (!ok)
    {
        result.success = false;
        if (result.error.empty())
        {
            result.error = "DeepSeek request failed";
        }
    }
    // 合并累计器中的分块工具调用为完整的 ToolCall
    for (auto &[index, acc] : tool_call_acc)
    {
        if (acc.name.empty())
        {
            continue; // 跳过没有 name 的
        }
        ToolCall call;
        call.id = acc.id;
        call.name = acc.name;
        try
        {
            call.arguments = acc.arguments.empty() ? json::object()
                                                   : json::parse(acc.arguments);
        }
        catch (const std::exception &e)
        {
            std::cerr << "[LLMService] Failed to parse arguments: " << e.what() << std::endl;
            call.arguments = json::object();
        }
        result.tool_calls.push_back(call);
    }
    result.has_tool_calls = !result.tool_calls.empty();

    return result;
}

// 本地 llama.cpp 调用，因本地模型不支持 tool_calls，仅占位，后续实现
LLMCallResult LLMService::call_llama(const std::string &session_id, const std::string &message, bool enable_tools,
                                     OutputCallback &emit, const std::atomic<bool> *canceled)
{
    // 置void消除编译警告
    (void)session_id;
    (void)message;
    (void)enable_tools;
    (void)emit;
    (void)canceled;
    // TODO: 与 call_deepseek 类似，只是 URL 和 header 不同
    LLMCallResult reslut;
    reslut.success = false;
    std::cerr << "[LLMService] llama.cpp backend not implemented yet" << std::endl;
    return reslut;
}

// 单行 SSE 解析
void LLMService::process_sse_line(const std::string &line, LLMCallResult &result, OutputCallback &emit, std::unordered_map<int, ToolCallAccumulator> &tool_call_acc)
{
    // 空行跳过
    if (line.empty())
        return;

    // 只处理 "data: " 开头的行,兼容 "data:"不带空格
    const std::string prefix = "data:";
    if (line.compare(0, prefix.size(), prefix) != 0)
    {
        return;
    }

    size_t pos = prefix.size();
    if (pos < line.size() && line[pos] == ' ')
        ++pos;

    std::string json_str = line.substr(pos);

    // 去掉尾行空白
    while (!json_str.empty() && (json_str.back() == '\n' || json_str.back() == '\r' || json_str.back() == ' '))
    {
        json_str.pop_back();
    }

    // 流结束标志
    if (json_str == "[DONE]")
    {
        return;
    }

    // 非 JSON 行，跳过
    if (json_str.empty() || json_str.front() != '{')
    {
        return;
    }

    try
    {
        json data = json::parse(json_str);
        if (!data.contains("choices") || !data["choices"].is_array() || data["choices"].empty())
        {
            return;
        }

        const auto &choice = data["choices"][0];
        if (!choice.contains("delta"))
        {
            return;
        }
        const auto &delta = choice["delta"];

        // 1.content
        if (delta.contains("content") && !delta["content"].is_null())
        {
            std::string content = delta["content"].get<std::string>();
            if (!content.empty())
            {
                result.content += content;
                emit(ChunkType::CONTENT, content);
            }
        }

        // 2.reasoning_content (推理部分)
        if (delta.contains("reasoning_content") && !delta["reasoning_content"].is_null())
        {
            std::string reasoning = delta["reasoning_content"].get<std::string>();
            if (!reasoning.empty())
            {
                result.reasoning_content += reasoning;
                emit(ChunkType::REASONING, reasoning);
            }
        }

        // 3.tool_calls
        if (delta.contains("tool_calls") && delta["tool_calls"].is_array())
        {
            for (const auto &tc : delta["tool_calls"])
            {
                int index = tc.value("index", 0);
                auto &acc = tool_call_acc[index];

                if (tc.contains("id") && !tc["id"].is_null())
                {
                    acc.id = tc["id"].get<std::string>();
                }
                if (tc.contains("function"))
                {
                    const auto &func = tc["function"];
                    if (func.contains("name") && !func["name"].is_null())
                    {
                        acc.name += func["name"].get<std::string>();
                    }
                    if (func.contains("arguments") && !func["arguments"].is_null())
                    {
                        acc.arguments += func["arguments"].get<std::string>();
                    }
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        // 失败只记录日记，不中断流
        std::cerr << "[LLMService] JSON parse error: " << e.what() << std::endl;
        std::cerr << "[LLMService] Failed line: " << json_str << std::endl;
    }
}
