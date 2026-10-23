#include "llm/AgentLoop.hpp"
#include "llm/SessionManager.hpp"
#include "core/ThreadPool.hpp"
#include "core/ClientConnection.hpp"
#include <iostream>

AgentLoop::AgentLoop(LLMService &llm, ToolExecutor &executor,
                     SessionManager &session, ThreadPool &thread_pool)
    : llm_(llm), executor_(executor), session_(session), thread_pool_(thread_pool) {}

// 推送内容的回调
OutputCallback AgentLoop::make_emit_callback(const ConnectionContext &ctx)
{
    // 只捕获 thread_pool_的引用和 weak_conn, 不捕获 this,确保 AgentLoop销毁时线程安全

    ThreadPool *tp = &thread_pool_;
    std::weak_ptr<ClientConnection> weak_conn = ctx.conn;

    return [tp, weak_conn](ChunkType type, const std::string &data)
    {
        tp->post_completion([weak_conn, type, data]()
                            {
            auto conn = weak_conn.lock();
            if(!conn){
                return; //链接已断，直接丢弃
            }

            std::string event;
            switch (type)
            {
            case ChunkType::CONTENT:
                event = "data: "+data+"\n\n";
                break;
            case ChunkType::REASONING:
            event = "event: reasoning\ndata: "+data+"\n\n";
            break;
            case ChunkType::TOOL_RESULT:
            event = "event: tool_result\ndata: "+data+"\n\n";
            
            default:
                break;
            }
            conn->send_data(event); });
    };
}

LLMCallResult AgentLoop::call_llm(const std::string &session_id, const std::string &message, bool enable_tools, OutputCallback emit)
{
    state_ = AgentState::CALLING_LLM;
    return llm_.call(session_id, message, enable_tools, emit);
}

void AgentLoop::execute_tools(const std::vector<ToolCall> &calls, const std::string &session_id, OutputCallback emit, const std::string &reasoning_content)
{
    state_ = AgentState::EXECUTING_TOOLS;

    // 1. 先保存 assintant 消息
    ChatMessage assistant_msg;
    assistant_msg.role = "assistant";
    assistant_msg.content = "";
    assistant_msg.reasoning_content = reasoning_content;
    for (const auto &call : calls)
    {
        assistant_msg.tool_calls.push_back({call.id, call.name, call.arguments.dump()});
    }

    session_.append_message(session_id, assistant_msg);

    // 2.依次执行工具，保存 tool 消息
    for (const auto &call : calls)
    {
        ToolResult result = executor_.execute_single(call);
        emit(ChunkType::TOOL_RESULT, result.result);

        ChatMessage tool_msg;
        tool_msg.role = "tool";
        tool_msg.tool_call_id = call.id; // 关联 assintant
        tool_msg.content = result.result;
        session_.append_message(session_id, tool_msg);
    }
}

void AgentLoop::execute_loop(const std::string &session_id, const std::string &message, const ConnectionContext &ctx)
{
    OutputCallback emit = make_emit_callback(ctx);

    while (should_continue())
    {
        ++iteration_;

        // 第一轮携带用户消息；后续轮次 message 为空 (历史已包含)
        std::string current_message = (iteration_ == 1) ? message : "";

        // 1.调用 LLM
        LLMCallResult result = call_llm(session_id, current_message, true, emit);

        if (!result.success)
        {
            set_error("LLM call failed");
            return;
        }

        // 2.第一轮成功后，保存用户信息(只保存一次)
        if (iteration_ == 1 && !message.empty())
        {
            session_.append_message(session_id, ChatMessage("user", message));
        }

        // 3.有工具调用，执行工具，继续循环
        if (result.has_tool_calls)
        {
            execute_tools(result.tool_calls, session_id, emit, result.reasoning_content);
            continue;
        }

        // 4.无工具调用,且最终已在LLMService推送，保存并结束对话
        if (!result.content.empty())
        {
            session_.append_message(session_id, ChatMessage("assistant", result.content));
        }

        state_ = AgentState::DONE;
        std::cout << "[AgentLoop] Done after " << iteration_ << " iteration(s)" << std::endl;
        return;
    }
    // 达到最大轮次
    set_error("Max iterations reached");
    emit(ChunkType::CONTENT, "\n[Error: 达到最大轮次]");
}

// 入口
void AgentLoop::run(const std::string &session_id, const std::string &message, const ConnectionContext &ctx)
{
    state_ = AgentState::IDLE;
    iteration_ = 0;

    execute_loop(session_id, message, ctx);
}

// 辅助
bool AgentLoop::should_continue() const
{
    return iteration_ < MAX_ITERATIONS && state_ != AgentState::DONE && state_ != AgentState::ERROR;
}

void AgentLoop::set_error(const std::string &reason)
{
    state_ = AgentState::ERROR;
    std::cerr << "[AgentLoop] Error: " << reason << std::endl;
}
