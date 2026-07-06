#include "protocol/SseHandler.hpp"
#include <sstream>

SseHandler::SseHandler(ClientConnection *conn, ThreadPool &tp) : conn_(conn), thread_pool_(tp) {}
void SseHandler::reset() {}

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