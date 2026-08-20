#include "tools/ToolExecutor.hpp"
#include <stdexcept>

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
    for (const auto &tc : tool_calls)
    {
        ToolCall call;
        if (tc.contains("id"))
        {
            call.id = tc["id"];
        }
        if (tc.contains("function") && tc["function"].contains("name"))
        {
            call.name = tc["function"]["name"];
        }
        if (tc.contains("function") && tc["function"].contains("arguments"))
        {
            call.arguments = json::parse(tc["function"]["arguments"].get<std::string>());
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