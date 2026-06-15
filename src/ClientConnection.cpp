#include "ClientConnection.hpp"
#include "HttpChannel.hpp"
#include "ProtocolParser.hpp"
#include <unistd.h>
#include <cerrno>
#include <iostream>
#include <sys/socket.h>

ClientConnection::ClientConnection(int fd, int id, ThreadPool &tp) : fd_(fd), conn_id_(id), thread_pool_(tp) {}

ClientConnection::~ClientConnection()
{
    if (fd_ != -1)
        close(fd_);
}

bool ClientConnection::on_readable()
{
    char buf[4096];
    bool need_close = false;

    // 读取数据
    while (true)
    {
        ssize_t n = recv(fd_, buf, sizeof(buf), 0);
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            perror("recv");
            need_close = true;
            break;
        }
        else if (n == 0)
        {
            std::cout << "Client " << conn_id_ << " closed connection." << std::endl;
            need_close = true;
            break;
        }
        else
        {
            read_buffer_.append(buf, n);
        }
    }

    if (need_close)
    {
        close_connection();
        return true;
    }

    // 协议检测（HTTP）
    if (protocol_type_ == ProtocolType::UNKNOWN && !read_buffer_.empty())
    {
        if (read_buffer_.find("GET ") == 0 || read_buffer_.find("POST ") == 0)
        {
            protocol_type_ = ProtocolType::HTTP;
            handler_ = std::make_unique<HttpChannel>(thread_pool_);
        }
        else if (read_buffer_.find("PING|") == 0 || read_buffer_.find("CHAT|") == 0)
        {
            protocol_type_ = ProtocolType::CUSTOM_LINE;

            // 为了统一，暂时不对自定义协议做处理
        }
        else
        {
            // 未知协议，关闭连接
            close_connection();
            return true;
        }
    }

    if (protocol_type_ == ProtocolType::HTTP)
    {
        need_close = handler_->process(fd_, read_buffer_);
        if (need_close)
            close_connection();
    }
    else if (protocol_type_ == ProtocolType::CUSTOM_LINE)
    {
        need_close = handler_->process(fd_, read_buffer_);
    }
    return need_close;
}

void ClientConnection::send_response(const std::string &response)
{
    ssize_t sent = send(fd_, response.c_str(), response.size(), 0);
    if (sent < 0)
    {
        perror("send");
        close_connection();
    }
}

void ClientConnection::close_connection()
{
    if (fd_ != -1)
    {
        close(fd_);
        fd_ = -1;
    }
}