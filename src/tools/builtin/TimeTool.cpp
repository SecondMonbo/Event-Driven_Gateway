#include "tools/builtin/TimeTool.hpp"
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

void register_time_tool(ToolRegistry &registry)
{
    registry.registry_tool(
        "get_current_time",
        "获取当前系统时间, 返回格式为 YYYY-MM-DD HH:MM:SS",
        {{"type", "object"},
         {"properties", json::object()},
         {"required", json::array()}},
        [](const json &args) -> std::string
        {
            (void)args;
            auto now = std::chrono::system_clock::now();
            std::time_t t = std::chrono::system_clock::to_time_t(now);

            // 转换为本地时间并格式化
            std::tm tm = *std::localtime(&t);
            std::ostringstream oss;
            oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
            return oss.str();
        });
}