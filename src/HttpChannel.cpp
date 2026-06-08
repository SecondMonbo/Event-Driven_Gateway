#include "HttpChannel.hpp"
#include <sstream>
#include <iostream>
#include <cctype>
#include <unistd.h>
#include <sys/socket.h>

bool HttpChannel::extract_line(std::string &buffer, std::string &line)
{
    size_t pos = buffer.find("\r\n");
    if (pos == std::string::npos)
        return false;
    line = buffer.substr(0, pos);
    buffer.erase(0, pos + 2);
    return true;
}

bool HttpChannel::parse_request_line(const std::string &line)
{
    std::istringstream iss(line);
    if (!(iss >> method_ >> path_ >> version_))
        return false;
    // 简单验证
    if (version_ != "HTTP/1.1")
        return false;
    return true;
}

bool HttpChannel::parse_header_line(const std::string &line)
{
    size_t colon = line.find(':');
    if (colon == std::string::npos)
        return false;
    std::string key = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    size_t start = value.find_first_not_of(" \t");
    if (start == std::string::npos)
        return false;
    value = value.substr(start);

    // 键转小写
    std::string lower_key;
    lower_key.reserve(key.size());
    for (char c : key)
        lower_key.push_back(tolower(c));

    if (lower_key == "content-length")
    {
        try
        {
            content_length_ = std::stoi(value);
            if (content_length_ < 0)
                return false;
        }
        catch (...)
        {
            return false;
        }
    }
    headers_[lower_key] = value;
    return true;
}

bool HttpChannel::parse_body(std::string &buffer)
{
    size_t need = content_length_ - body_.size();
    if (buffer.size() >= need)
    {
        body_.append(buffer, 0, need);
        buffer.erase(0, need);
        state_ = PARSE_DONE;
        return true;
    }
    else
    {
        body_.append(buffer);
        buffer.clear();
        return false;
    }
}

void HttpChannel::send_error_response(int fd, int status_code)
{
    std::string body = "<h1>Error</h1>";
    std::string response = "HTTP/1.1 " + std::to_string(status_code) + " Bad Request\r\n"
                                                                       "Content-Length: " +
                           std::to_string(body.size()) + "\r\n"
                                                         "Connection: close\r\n\r\n" +
                           body;
    send(fd, response.c_str(), response.size(), 0);
}

std::string HttpChannel::generate_response()
{
    std::string body = "Hello, HTTP!";
    if (method_ == "POST")
    {
        body = "Received body: " + body_;
    }
    std::string response = "HTTP/1.1 200 OK\r\n"
                           "Content-Length: " +
                           std::to_string(body.size()) + "\r\n"
                                                         "Connection: " +
                           (headers_["connection"] == "keep-alive" ? "keep-alive" : "close") + "\r\n"
                                                                                               "\r\n" +
                           body;
    return response;
}

void HttpChannel::reset()
{
    state_ = PARSE_REQUEST_LINE;
    method_.clear();
    path_.clear();
    version_.clear();
    headers_.clear();
    body_.clear();
    content_length_ = 0;
}

bool HttpChannel::process(int client_fd, std::string &read_buffer)
{
    while (state_ != PARSE_DONE)
    {
        switch (state_)
        {
        case PARSE_REQUEST_LINE:
        {
            std::string line;
            if (!extract_line(read_buffer, line))
                return false;
            if (!parse_request_line(line))
            {
                send_error_response(client_fd, 400);
                return true;
            }
            state_ = PARSE_HEADERS;
            break;
        }
        case PARSE_HEADERS:
        {
            std::string line;
            if (!extract_line(read_buffer, line))
                return false;
            if (line.empty())
            {
                if (method_ == "POST" && content_length_ > 0)
                    state_ = PARSE_BODY;
                else
                    state_ = PARSE_DONE;
                break; // 关键：跳过下面的头部解析
            }
            if (!parse_header_line(line))
            {
                send_error_response(client_fd, 400);
                return true;
            }
            break;
        }
        case PARSE_BODY:
        {
            if (!parse_body(read_buffer))
                return false;
            break;
        }
        default:
            return true;
        }
    }

    // 生成并发送响应
    std::string response = generate_response();
    send(client_fd, response.c_str(), response.size(), 0);

    // Keep-Alive 处理
    bool keep_alive = false;
    auto it = headers_.find("connection");
    if (it != headers_.end() && it->second == "keep-alive")
        keep_alive = true;

    if (keep_alive)
    {
        reset();
        // 递归调用处理下一个请求（注意栈深度，简单场景可用）
        return process(client_fd, read_buffer);
    }
    else
    {
        return true; // 关闭连接
    }
}