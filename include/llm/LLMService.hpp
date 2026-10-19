#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <unordered_map>

#include "llm/SessionManager.hpp"
#include "llm/HttpClient.hpp"
#include "core/ConnectionContext.hpp"
#include "tools/ToolExecutor.hpp"

// 前向声明
class ConnectionContext;

/**
 * @brief 推送类型
 */

enum class PushType
{
    CONTENT,    // 最终回答
    REASONING,  // 思考过程
    TOOL_RESULT // 工具结果
};

/**
 * @brief 推送回调
 *
 * LLMService 通过这个回调提交流式数据
 * 由 AgentLoop 注入，内部通过 ThreadPool::post_completion 投递到主线程
 */
using PushCallback = std::function<void(PushType type, const std::string &data)>;

/**
 * @brief 单次 LLM 调用的结果
 */
struct LLMCallResult
{
    bool has_tool_calls = false;
    std::vector<ToolCall> tool_calls;
    std::string content; // 累积的最终回答
    bool success = true;
};

/**
 * @brief 模型后端类型
 */
enum class ModelBackend
{
    LOCAL_LLAMA,
    DEEPSEEK_API
};

/**
 * @brief LLM 服务层 --- 单次调用
 *
 * 职责：
 *   - 构造请求体
 *   - 通过 HttpClient 调用本地或远程 LLM 服务
 *   - 解析 SSE 流：content/reasoning 通过 push 回调推送，tool_calls 累计返回
 */

class LLMService
{
public:
    /**
     * @brief 构造函数
     * @param session_manager 会话管理器引用 （由上层持有）
     */

    explicit LLMService(SessionManager &session_manager, ToolRegistry &tool_registry);
    ~LLMService() = default;

    LLMService(const LLMService &) = delete;
    LLMService &operator=(const LLMService &) = delete;

    void set_backend(ModelBackend backend) { backend_ = backend; };
    ModelBackend get_backend() const { return backend_; };

    /**
     * @brief 单次调用 LLM（流式）
     *
     * 同步阻塞，将由工作线程执行
     *
     * @param session_id   会话 ID
     * @param message      用户消息 (仅首次非空)
     * @param enable_tools 是否携带工具定义
     * @param push         推送回调 (由 AgentLoop 注入)
     *@return LLMCallResult
     */
    LLMCallResult call(const std::string &session_id,
                       const std::string &message,
                       bool enable_tools,
                       PushCallback push);

private:
    // 依赖
    SessionManager &session_manager_;
    HttpClient http_client_;

    // 工具调用成员
    ToolRegistry &tool_registry_;

    ModelBackend backend_ = ModelBackend::DEEPSEEK_API;

    // 内部实现
    LLMCallResult call_deepseek(const std::string &session_id,
                                const std::string &message,
                                bool enable_tools,
                                PushCallback &push);

    LLMCallResult call_llama(const std::string &session_id,
                             const std::string &message,
                             bool enable_tools,
                             PushCallback &push);

    std::string build_request_body(const std::string &session_id,
                                   const std::string &message,
                                   bool enable_tools);

    // 工具调用累计器 (流式响应中 tool_calls 分块到达)
    struct ToolCallAccumulator
    {
        std::string id;
        std::string name;
        std::string arguments;
    };

    void process_sse_line(const std::string &line,
                          LLMCallResult &result,
                          PushCallback &push,
                          std::unordered_map<int, ToolCallAccumulator> &tool_call_acc);
};