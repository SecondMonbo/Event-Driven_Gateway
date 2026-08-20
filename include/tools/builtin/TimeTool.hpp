#pragma once

#include "tools/ToolRegistry.hpp"

/**
 * @brief 注册获取当前时间的工具
 *
 * 无参数，返回当前系统时间 （格式： YYYY-MM-DD HH:MM::SS)
 */

void register_time_tool(ToolRegistry &registry);