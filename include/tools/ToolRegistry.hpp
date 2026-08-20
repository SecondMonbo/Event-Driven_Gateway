#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include "tools/ToolDefinition.hpp"

// 工具执行函数类型：接收 JSON 参数，返回字符串结果
using ToolFunction = std::function<std::string(const json &)>;

/**
 * @brief 工具注册表
 *
 * 职责：
 * 1.存储已注册的工具
 * 2.提供查询接口->LLM
 * 3.提供执行接口->gateway
 *
 * 示例:
 *   ToolRegistry resgistry;
 *   registry.registry_tool("get_time", "获取当前时间", {}, [](const json&){
 *   return "10:00";});
 *   auto defs = registry.get_all_definitions();  // LLM
 *   auto result = registyr.execute("get_time", {}); //执行工具
 */

class ToolRegistry
{
public:
    // 注册工具
    void registry_tool(const std::string &name,
                       const std::string &description,
                       const json &parameters,
                       ToolFunction func);

    // 批量注册
    void registry_tools(const std::vector<ToolDefinition> &defs,
                        const std::vector<ToolFunction> &funcs);

    // 获取所有工具定义
    std::vector<ToolDefinition> get_all_definitions() const;

    // 检查工具是否存在
    bool exists(const std::string &name) const;

    // 执行工具
    std::string execute(const std::string &name, const json &args);

private:
    std::unordered_map<std::string, ToolDefinition> definitions_;
    std::unordered_map<std::string, ToolFunction> functions_;
};