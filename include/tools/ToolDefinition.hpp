#pragma once

#include <string>
#include <third_party/json.hpp>

using json = nlohmann::json;

/**
 * @brief 工具定义结构体
 *
 * 定义：
 * 1.工具名称
 * 2.工具作用（描述）
 * 3.调用参数（JSON Schema 格式）
 *
 * 示例
 *   ToolDefinition def;
 *   def.name = "get_current_time";
 *   def.descriprtion = "获取当前系统时间"；
 *   def.parameters = {
 *       {"type", "object"},
 *       {"properties", json::object()}
 *   }
 */

struct ToolDefinition
{
    std::string name;        // 工具名称 （唯一标识）
    std::string description; // 工具描述 （告知模型工具用途）
    json parameters;         // 参数 Schema (JSON Schema 格式)
};
