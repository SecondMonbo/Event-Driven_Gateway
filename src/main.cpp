#include "core/EpollLoop.hpp"
#include "llm/SessionManager.hpp"
#include "llm/LLMService.hpp"
#include <iostream>

int main()
{
    // 会话管理器 (全局单一实例)
    SessionManager session_manager;

    // 创建大模型服务
    LLMService llm_service(session_manager);

    EpollLoop loop(llm_service);
    if (!loop.init(12010))
    {
        std::cerr << "Failed to initialize gateway" << std::endl;
        return 1;
    }
    loop.run();
    return 0;
}