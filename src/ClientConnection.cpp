#include "ClientConnection.hpp"
#include "HttpChannel.hpp"
#include "ProtocolParser.hpp"
#include "Dispatcher.hpp"
#include <unistd.h>
#include <cerrno>
#include <iostream>
#include <sys/socket.h>

ClientConnection::ClientConnection(int fd, int id) : fd_(fd), conn_id_(id) {}

ClientConnection::~ClientConnection() = default;

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
    if (!http_channel_)
    {
        if (read_buffer_.find("GET ") == 0 || read_buffer_.find("POST ") == 0 ||
            read_buffer_.find("PUT ") == 0 || read_buffer_.find("HEAD ") == 0)
        {
            http_channel_ = std::make_unique<HttpChannel>();
        }
    }

    if (http_channel_)
    {
        need_close = http_channel_->process(fd_, read_buffer_);
        if (need_close)
            close_connection();
    }
    else
    {
        // 自定义行协议
        std::string line;
        while (ProtocolParser::extract_line(read_buffer_, line))
        {
            Message msg = ProtocolParser::parse(conn_id_, line);
            std::string resp = Dispatcher::dispatch(msg) + "\n";
            send_response(resp);
        }
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