#include "protocol/SseHandler.hpp"
#include "llm/LLMService.hpp"
#include <sstream>
#include <cstdio>
#include <chrono>
#include <ctime>

SseHandler::SseHandler(const ConnectionContext &ctx) : ctx_(ctx), is_chat_(ctx.pending_llm.valid)
{
    // std::cout << "sse upgrade successfully\n";
}
void SseHandler::reset() {}

SseHandler::~SseHandler()
{
    close();
}

std::string SseHandler::format_sse_message(const std::string &data, const std::string &event_type, const std::string &id)
{
    std::ostringstream oss;

    // 1.可选event字段
    if (!event_type.empty())
    {
        oss << "event: " << event_type << '\n';
    }

    // 2.可选id字段
    if (!id.empty())
    {
        oss << "id: " << id << '\n';
    }

    // 3.按行（'\n'）拆分data字段
    std::string::size_type start = 0;
    std::string::size_type end;
    while ((end = data.find('\n', start)) != std::string::npos)
    {
        oss << "data: " << data.substr(start, end - start) << '\n';
        start = end + 1;
    }

    // 处理最后一行
    if (start < data.size())
    {
        oss << "data: " << data.substr(start) << '\n';
    }
    else if (data.empty())
    {
        oss << "data: \n";
    }

    // 4.消息结束
    oss << "\n";

    return oss.str();
}

std::string SseHandler::chunk(const std::string &data)
{
    if (data.empty())
        return {};

    char head[32];
    int n = std::snprintf(head, sizeof(head), "%zx\r\n", data.size());

    std::string frame;
    frame.reserve(n + data.size() + 2);
    frame.append(head, n);
    frame.append(data);
    frame.append("\r\n");
    return frame;
}

std::string SseHandler::chunk_end()
{
    return "0\r\n\r\n";
}
bool SseHandler::begin_stream()
{
    if (handshake_sent_)
        return true;
    if (closed_)
        return false;
    std::string conn = is_chat_ ? "close" : "keep-alive";

    std::string header = "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/event-stream\r\n"
                         "Cache-Control: no-cache, no-transform\r\n"
                         "Transfer-Encoding: chunked\r\n"
                         "Connection: " +
                         conn + "\r\n"
                                "X-Accel-Buffering: no\r\n"
                                "\r\n";
    if (!ctx_.send_data(header))
    {
        return false;
    }
    handshake_sent_ = true;

    return true;
}
bool SseHandler::send_event(const std::string &data, const std::string &event_type, const std::string &id)
{
    if (!begin_stream())
        return false;

    // 格式化并发送消息
    std::string message = format_sse_message(data, event_type, id);
    // 绕过队列，直接发送
    if (!ctx_.send_data(SseHandler::chunk(message)))
    {
        return false;
    }
    return true;
}

bool SseHandler::push_event(const std::string &data, const std::string &event_type, const std::string &id)
{
    if (closed_)
        return false;

    std::string message = format_sse_message(data, event_type, id);

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        pending_events_.push(std::move(message));
    }
    if (auto conn = ctx_.get_conn())
    {
        conn->wakeup();
    }
    else
    {
        return false;
    }
    return true;
}

void SseHandler::start_heartbeat()
{
    if (is_chat_ || closed_ || timer_id_ != 0)
        return;
    timer_id_ = ctx_.timer_manager.add_timer(3000, [this]()
                                             {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::string time_str = std::ctime(&t);
        time_str.pop_back();
        push_event("Current time: "+time_str,"heartbeat",std::to_string(t)); }, 3000);
}

void SseHandler::close()
{
    if (closed_)
        return;
    closed_ = true;

    // 取消定时器
    if (timer_id_ != 0)
    {
        ctx_.timer_manager.cancel_timer(timer_id_);
        timer_id_ = 0;
    }
    // 清空待发送时间列表(释放内存)
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        std::queue<std::string> empty;
        pending_events_.swap(empty);
    }
}

bool SseHandler::send_error_event(const std::string &error_msg)
{
    if (closed_)
    {
        return false;
    }
    std::string event = format_sse_message(error_msg, "error");
    return ctx_.send_data(SseHandler::chunk(event));
}

ProcessResult SseHandler::process(std::string &read_buffer)
{
    (void)read_buffer; // SSE不需要读取客户端数据，将变量转为（void）类型，避免编译器unused variable()未使用变量警告

    if (!handshake_sent_)
    {
        if (!begin_stream())
            return ProcessResult::CLOSE;
        // if (!is_chat_)
        //     start_heartbeat();
    }

    // 取出请求数据并调用 LLMService
    if (ctx_.pending_llm.valid)
    {
        std::string session_id = ctx_.pending_llm.session_id;
        std::string message = ctx_.pending_llm.message;
        ctx_.pending_llm.valid = false;

        if (ctx_.chat_handler)
        {
            ctx_.chat_handler(session_id, message, ctx_);
        }
        else
        {
            send_error_event("chat handler not configured");
            ctx_.send_data(SseHandler::chunk(format_sse_message("[DONE]", "done")));
            if (auto conn = ctx_.get_conn())
            {
                conn->send_data(SseHandler::chunk_end());
                conn->request_close();
            }
        }
    }

    return ProcessResult::CONTINUE;
}