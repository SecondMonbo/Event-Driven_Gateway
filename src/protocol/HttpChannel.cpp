#include "HttpChannel.hpp"
#include <sstream>
#include <iostream>
#include <cctype>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fstream>
#include <limits.h>

HttpChannel::HttpChannel(ThreadPool &tp) : thread_pool_(tp)
{
    // 其他成员使用类内初始化器，无需显式初始化
}

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

std::string HttpChannel::get_executable_dir()
{
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count == -1)
        return ".";
    std::string exe_path(result, count);
    return exe_path.substr(0, exe_path.rfind('/'));
}

std::string HttpChannel::generate_error_response(int status_code)
{

    std::string status_text;
    switch (status_code)
    {
    case 400:
        status_text = "Bad Request";
        break;
    case 404:
        status_text = "Not Found";
        break;
    case 405:
        status_text = "Method Not Allowed";
        break;
    case 500:
        status_text = "Internal Server Error";
        break;
    default:
        status_text = "Error";
    }
    std::string body = "<h1>" + status_text + "</h1>";
    std::string response = "HTTP/1.1 " + std::to_string(status_code) + " " + status_text + "\r\n"
                                                                                           "Content-Length: " +
                           std::to_string(body.size()) + "\r\n"
                                                         "Connection: close\r\n"
                                                         "\r\n" +
                           body;
    return response; // 返回完整的 HTTP 响应
}

std::string HttpChannel::get_mime_type(const std::string &path)
{
    static const std::unordered_map<std::string, std::string> mime_map = {
        {".html", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".txt", "text/plain"}};
    size_t dot = path.rfind('.');
    if (dot == std::string::npos)
        return "application/octet-stream";
    std::string ext = path.substr(dot);
    auto it = mime_map.find(ext);
    if (it != mime_map.end())
        return it->second;
    return "application/octet-stream";
}

std::string HttpChannel::generate_response()
{
    if (method_ == "GET")
    {
        std::string base_dir = get_executable_dir() + "/../www";
        std::string file_path = base_dir + path_;
        if (path_ == "/")
            file_path = base_dir + "/index.html";

        struct stat st;
        if (stat(file_path.c_str(), &st) == 0 && S_ISREG(st.st_mode))
        {
            std::string content_type = get_mime_type(file_path);
            std::ifstream file(file_path, std::ios::binary);
            std::string body((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()); // 多加一层括号，避免Most Vexing Parse，明确是一个对象构造
            std::string response = "HTTP/1.1 200 OK\r\n"
                                   "Content-Type: " +
                                   content_type + "\r\n"
                                                  "Content-Length: " +
                                   std::to_string(body.size()) + "\r\n"
                                                                 "Connection: close\r\n"
                                                                 "\r\n" +
                                   body;
            return response;
        }
        else
        {
            return generate_error_response(404);
        }
    }

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
                std::string error_reponse = generate_error_response(400);
                send(client_fd, error_reponse.c_str(), sizeof(error_reponse), 0);
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