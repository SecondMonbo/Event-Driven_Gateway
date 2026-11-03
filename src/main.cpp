#include "core/EpollLoop.hpp"
#include "llm/SessionManager.hpp"
#include "llm/LLMService.hpp"
#include "tools/ToolRegistry.hpp"
#include "tools/builtin/TimeTool.hpp"
#include "tools/ToolExecutor.hpp"
#include <iostream>
#include <cstdlib>
#include <cerrno>

namespace
{
    int read_port()
    {
        const char *raw = std::getenv("GATEWAY_PORT");
        if (!raw || *raw == '\0')
            return 12010;

        char *end = nullptr;
        errno = 0;
        long value = std::strtol(raw, &end, 10);

        if (errno != 0 || end == raw || *end != '\0' || value <= 0 || value > 65535)
        {
            std::cerr << "Invalid GATEWAY_PORT: " << raw << std::endl;
            std::exit(1);
        }

        return static_cast<int>(value);
    }
}

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

    int port = read_port();
    if (!loop.init(port))
    {
        std::cerr << "Failed to initialize gateway" << std::endl;
        return 1;
    }
    loop.run();
    curl_global_cleanup();
    return 0;
}