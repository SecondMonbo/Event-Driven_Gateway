#include "protocol/SseHandler.hpp"
#include "llm/LLMService.hpp"
#include <sstream>

SseHandler::SseHandler(const ConnectionContext &ctx) : conn_(ctx.conn), ctx_(ctx)
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

void SseHandler::send_event(const std::string &data, const std::string &event_type, const std::string &id)
{
    if (closed_)
        return;
    if (!handshake_sent_)
    {
        std::string header = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/event-stream\r\n"
                             "Cache-Control: no-cache\r\n"
                             "Connection: keep-alive\r\n"
                             "\r\n";
        conn_->send_data(header);
        handshake_sent_ = true;
    }

    // 格式化并发送消息
    std::string message = format_sse_message(data, event_type, id);
    // 绕过队列，直接发送
    conn_->send_data(message);
}

void SseHandler::push_event(const std::string &data, const std::string &event_type, const std::string &id)
{
    if (closed_)
        return;

    std::string message = format_sse_message(data, event_type, id);

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        pending_events_.push(std::move(message));
    }
    conn_->wakeup();
}

void SseHandler::start_heartbeat()
{
    if (timer_id_ != 0)
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

void SseHandler::send_error_event(const std::string &error_msg)
{
    std::string event = "data: {\"error\": \"" + error_msg + "\"}\n\n";
    conn_->send_data(event);
}

ProcessResult SseHandler::process(std::string &read_buffer)
{
    (void)read_buffer; // SSE不需要读取客户端数据，将变量转为（void）类型，避免编译器unused variable()未使用变量警告

    if (closed_)
        return ProcessResult::CLOSE;

    if (!handshake_sent_)
    {
        std::string header = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/event-stream\r\n"
                             "Cache-Control: no-cache\r\n"
                             "Connection: keep-alive\r\n"
                             "\r\n";
        conn_->send_data(header);
        handshake_sent_ = true;

        // 启动心跳
        // std::cout << "心跳启动\n";
        // start_heartbeat();
    }

    // 取出请求数据并调用 LLMService
    std::string session_id = ctx_.pending_llm.session_id;
    std::string message = ctx_.pending_llm.message;
    ctx_.pending_llm.valid = false;
    if (ctx_.llm_service)
    {
        ctx_.llm_service->handle_chat_with_tools(session_id, message, ctx_);
    }
    else
    {
        send_error_event("LLM service not available");
        return ProcessResult::CLOSE;
    }

    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!pending_events_.empty())
    {
        std::string message = std::move(pending_events_.front());
        pending_events_.pop();
        conn_->send_data(message);
    }

    return ProcessResult::CONTINUE;
}