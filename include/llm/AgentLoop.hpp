#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>

#include "llm/LLMService.hpp"
#include "core/ConnectionContext.hpp"
#include "tools/ToolExecutor.hpp"
#include "tools/ToolRegistry.hpp"

class SessionManager;
class ThreadPool;

/**
 * @brief Agent 运行状态
 *
 * 用以显式管理循环流程，避免递归导致栈溢出的风险
 */

enum class AgentState
{
    IDLE,            // 空闲
    CALLING_LLM,     // 正在调用 LLM
    EXECUTING_TOOLS, // 正在执行工具
    DONE,            // 正常结束
    ERROR            // 出错
};

/**
 * @brief Agent Loop -- 对话流程调度器
 *
 * 仅承担流程管理，具体任务将交由其他模块负责,为避免阻塞主线程，运行时将交由工作线程
 * 数据推送将由 OutputCallback推送至ThreadPool
 *
 * 职责：
 *   1. 循环控制：决定是否继续调用 LLM 还是结束对话
 *   2. 工具调度: 从 LLM 响应中提取 tool_calls, 执行工具，将结果写回历史
 *   3. 终止判断：满足条件是停止循环
 *
 * 使用示例：
 *   AgentLoop loop(llm_service, tool_executor, session_manager);
 *   loop.run(session_id, message, ctx);
 */

// enble_shared_from_this 未来如果有异步回调可以用到
class AgentLoop : public std::enable_shared_from_this<AgentLoop>
{
public:
    AgentLoop(LLMService &llm,
              ToolExecutor &executor,
              SessionManager &session,
              ThreadPool &thread_pool);
    ~AgentLoop() = default;

    // 禁止拷贝，每个连接的会话独有
    AgentLoop(const AgentLoop &) = delete;
    AgentLoop &operator=(const AgentLoop &) = delete;

    /**
     * @brief 启动一次完整的对话循环
     *
     * 流程：
     *   1. 调 LLM (携带工具定义)
     *   2. 如果 LLM 返回 tool_calls -> 执行工具 -> 写回历史 -> 回到步骤1
     *   3. 如果 LLM 返回content -> 流式推送 ->结束
     *
     * @param session_id 会话 ID
     * @param message    用户消息 (首次调用时非空，后续轮次为空)
     * @param ctx        连接上下文
     */
    void run(const std::string &session_id,
             const std::string &message,
             const ConnectionContext &ctx);

private:
    // 依赖(引用，不持有)
    LLMService &llm_;
    ToolExecutor &executor_;
    SessionManager &session_;
    ThreadPool &thread_pool_; // 作为资源被循环直接持有

    // 状态
    AgentState state_ = AgentState::IDLE;
    int iteration_ = 0;
    static constexpr int MAX_ITERATIONS = 10;

    /**
     * @brief 执行循环核心逻辑
     *
     * @param session_id 会话 ID
     * @param message    初始用户消息
     * @param ctx        连接上下文
     */
    void execute_loop(const std::string &session_id,
                      const std::string &message,
                      const ConnectionContext &ctx);

    /**
     * @brief 调用一次 LLM (同步，阻塞当前线程)
     *
     * @param session_id   会话 ID
     * @param message      用户消息 (仅首次非空)
     * @param enable_tools 是否携带工具定义
     * @param emit         内容推送
     * @return true        表示本轮有工具调用
     * @return false       表示本轮是最终回答
     */
    LLMCallResult call_llm(const std::string &session_id,
                           const std::string &message,
                           bool enable_tools,
                           OutputCallback emit);

    /**
     * @brief  执行工具调用
     *
     * @param calls       工具调用列表
     * @param session_id  会话 ID (用于写回历史)
     * @param emit        用于推送工具结果
     */
    void execute_tools(const std::vector<ToolCall> &calls,
                       const std::string &session_id,
                       OutputCallback emit,
                       const std::string &reasoning_content);

    // 构造推送回调
    OutputCallback make_emit_callback(const ConnectionContext &ctx);

    /**
     * @brief 判断是否应该继续循环
     */
    bool should_continue() const;

    /**
     * @brief 设置错误状态
     */
    void set_error(const std::string &reason);
};
