#include "core/EpollLoop.hpp"
#include "llm/SessionManager.hpp"
#include "llm/LLMService.hpp"
#include "tools/ToolRegistry.hpp"
#include "tools/builtin/TimeTool.hpp"
#include "tools/ToolExecutor.hpp"
#include <iostream>

int main()
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
    // 会话管理器 (全局单一实例)
    SessionManager session_manager;

    // 工具注册表
    ToolRegistry tool_registry;
    register_time_tool(tool_registry);
    ToolExecutor tool_executor(tool_registry);

    // 创建大模型服务
    LLMService llm_service(session_manager, tool_registry);
    llm_service.set_backend(ModelBackend::DEEPSEEK_API);

    EpollLoop loop(llm_service, tool_executor, session_manager);
    if (!loop.init(12010))
    {
        std::cerr << "Failed to initialize gateway" << std::endl;
        return 1;
    }
    loop.run();
    curl_global_cleanup();
    return 0;
}