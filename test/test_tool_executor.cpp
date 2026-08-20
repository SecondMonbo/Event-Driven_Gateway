#include "tools/ToolRegistry.hpp"
#include "tools/ToolExecutor.hpp"
#include "tools/builtin/TimeTool.hpp"
#include <iostream>

using json = nlohmann::json;

int main()
{
    // 1. 初始化 Registry 并注册工具
    ToolRegistry registry;
    register_time_tool(registry);

    // 2. 创建 Executor
    ToolExecutor executor(registry);

    // 3. 模拟 LLM 返回的 tool_calls JSON（从 llama.cpp 流式响应中截取）
    std::string mock_response = R"({
        "choices": [{
            "delta": {
                "tool_calls": [{
                    "id": "call_123",
                    "function": {
                        "name": "get_current_time",
                        "arguments": "{}"
                    }
                }]
            }
        }]
    })";

    json data = json::parse(mock_response);

    // 4. 执行工具调用
    auto results = executor.execute_from_json(data);

    // 5. 打印结果
    std::cout << "=== Tool Execution Results ===" << std::endl;
    for (const auto &r : results)
    {
        std::cout << "tool_call_id: " << r.tool_call_id << std::endl;
        std::cout << "success: " << (r.success ? "true" : "false") << std::endl;
        std::cout << "result: " << r.result << std::endl;
        std::cout << "---" << std::endl;
    }

    return 0;
}