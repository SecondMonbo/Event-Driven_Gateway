#include "tools/ToolExecutor.hpp"
#include <stdexcept>
#include <iostream>

ToolExecutor::ToolExecutor(ToolRegistry &registry) : registry_(registry) {}

std::vector<ToolCall> ToolExecutor::parse_tool_calls(const json &data)
{
    std::vector<ToolCall> calls;
    if (!data.contains("choices") || data["choices"].empty())
    {
        return calls;
    }

    const auto &choice = data["choices"][0];
    if (!choice.contains("delta") || !choice["delta"].contains("tool_calls"))
    {
        return calls;
    }

    const auto &tool_calls = choice["delta"]["tool_calls"];
    std::cout << "parse_tool_calls: found " << tool_calls.size() << " tool calls" << std::endl;

    for (const auto &tc : tool_calls)
    {
        ToolCall call;
        // 打印完整 tc 结构，方便调试
        std::cout << "tc dump: " << tc.dump() << std::endl;

        if (tc.contains("id"))
        {
            call.id = tc["id"].get<std::string>();
        }

        if (tc.contains("function") && tc["function"].is_object())
        {
            const auto &func = tc["function"];
            std::cout << "func dump: " << func.dump() << std::endl;

            if (func.contains("name") && !func["name"].is_null())
            {
                call.name = func["name"].get<std::string>();
                // 如果 name 为空，跳过该工具调用
                if (call.name.empty())
                {
                    std::cerr << "Warning: empty tool name, skipping" << std::endl;
                    continue;
                }
                std::cout << "extracted name: " << call.name << std::endl;
            }
            else
            {
                std::cerr << "Warning: function.name missing in tool_call" << std::endl;
                continue;
            }

            if (func.contains("arguments") && !func["arguments"].is_null())
            {
                // arguments 可能是字符串，需要解析为 JSON
                std::string args_str = func["arguments"].get<std::string>();
                try
                {
                    call.arguments = json::parse(args_str);
                }
                catch (const std::exception &e)
                {
                    std::cerr << "Failed to parse arguments: " << e.what() << std::endl;
                    // 如果解析失败，设为空对象
                    call.arguments = json::object();
                }
            }
            else
            {
                call.arguments = json::object();
            }
        }
        else
        {
            std::cerr << "Warning: tool_call missing 'function' field or not object" << std::endl;
        }

        calls.push_back(call);
    }
    return calls;
}

ToolResult ToolExecutor::execute_single(const ToolCall &call)
{
    ToolResult result;
    result.tool_call_id = call.id;
    try
    {
        result.result = registry_.execute(call.name, call.arguments);
        result.success = true;
    }
    catch (const std::exception &e)
    {
        result.result = "Error: " + std::string(e.what());
        result.success = false;
    }
    return result;
}

std::vector<ToolResult> ToolExecutor::execute_from_json(const json &data)
{
    auto calls = parse_tool_calls(data);
    std::vector<ToolResult> results;
    results.reserve(calls.size());
    for (const auto &call : calls)
    {
        results.push_back(execute_single(call));
    }
    return results;
}