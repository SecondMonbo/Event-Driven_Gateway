#pragma once

#include <string>
#include <vector>
#include <third_party/json.hpp>
#include "tools/ToolRegistry.hpp"

using json = nlohmann::json;

/**
 * @brief 一次工具请求调用(从 LLM 解析得到)
 */
struct ToolCall
{
    std::string id;   // 工具调用 ID (LLM 生成)
    std::string name; // 工具名
    json arguments;   // 参数 (JSON 对象)
};

/**
 * @brief 一次工具调用的执行结果
 */
struct ToolResult
{
    std::string tool_call_id; // 对应的 ToolCall.id
    std::string result;       // 执行结果
    bool success;             // 是否成功
};

class ToolExecutor
{
public:
    /**
     * @brief 构造函数
     * @param registry 工具注册表引用
     */
    explicit ToolExecutor(ToolRegistry &registry);

    /**
     * @brief 从 JSON 中提取 tool_calls 并执行
     * @param data LLM 返回的 JSON 对象 (包含 choices 字段)
     * @return 执行结果列表
     */
    std::vector<ToolResult> execute_from_json(const json &data);

    /**
     * @brief 直接执行单个工具调用 (可独立使用)
     */
    ToolResult execute_single(const ToolCall &call);

private:
    ToolRegistry &registry_;

    // 辅助: 从 JSON 中解析 tool_calls
    std::vector<ToolCall> parse_tool_calls(const json &data);
};
