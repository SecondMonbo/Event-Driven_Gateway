#include "ClientConnection.hpp"
#include "HttpChannel.hpp"
#include <unistd.h>
#include <cerrno>
#include <iostream>
#include <sys/socket.h>

ClientConnection::ClientConnection(int fd, int id, ThreadPool &tp, EpollLoop *const loop) : fd_(fd), conn_id_(id), thread_pool_(tp), loop_(loop) {}

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

void ClientConnection::handle_write()
{
    if (write_buffer_.empty())
    {
        if (write_registered_)
        {
            loop_->remove_write_event(fd_); // 关键：取消内核中的 EPOLLOUT
            write_registered_ = false;
        }
        return;
    }
    int n = ::send(fd_, write_buffer_.data(), write_buffer_.size(), MSG_DONTWAIT);
    if (n < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        else
        {
            close_connection();
            return;
        }
    }
    if (n == (int)write_buffer_.size())
    {
        write_buffer_.clear();
        if (write_registered_)
        {
            loop_->remove_write_event(fd_);
            write_registered_ = false;
        }
    }
    else
    {
        write_buffer_.erase(0, n);
    }
}

void ClientConnection::send_data(const std::string &data)
{
    bool was_empty = write_buffer_.empty();
    write_buffer_.append(data);

    if (was_empty)
    {
        // 之前没有待发送数据，现在尝试立即发送
        int n = ::send(fd_, write_buffer_.data(), write_buffer_.size(), MSG_DONTWAIT); // MSG_DONTWAIT表示发送不了就不阻塞直接返回，“::”表全局作用域，避免冲突
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 缓冲区满，注册下次再试
                loop_->add_write_event(fd_);
                write_registered_ = true;
            }
            else
            {
                close_connection();
            }
            return;
        }

        if (n == write_buffer_.size())
        {
            write_buffer_.clear();
        }
        else
        {
            write_buffer_.erase(0, n);
            loop_->add_write_event(fd_);
            write_registered_ = true;
        }
    }
}