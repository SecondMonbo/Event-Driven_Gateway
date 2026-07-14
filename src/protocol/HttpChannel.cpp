#include "protocol/HttpChannel.hpp"
#include <sstream>
#include <iostream>
#include <cctype>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fstream>
#include <limits.h>
#include "third_party/json.hpp"
#include "llm/LLMService.hpp"

using json = nlohmann::json;

HttpChannel::HttpChannel(ConnectionContext ctx) : ctx_(ctx)
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

void HttpChannel::generate_response()
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
            // 引入写缓冲区，修改发送逻辑支持较大文件发送
            // 1.发送响应头，不含body
            std::string header = "HTTP/1.1 200 OK\r\n"
                                 "Content-Type: " +
                                 get_mime_type(file_path) + "\r\n"
                                                            "Content-Length: " +
                                 std::to_string(st.st_size) + "\r\n"
                                                              "Connection: " +
                                 (headers_["connection"] == "keep-alive" ? "keep-alive" : "close") +
                                 "\r\n"
                                 "\r\n";
            ctx_.conn->send_data(header);
            // 2.分块读取文件并发送
            std::ifstream file(file_path, std::ios::binary);
            if (!file.is_open())
            {
                ctx_.conn->send_data(generate_error_response(404));
            }
            const size_t CHUNK_SIZE = 64 * 1024; // 64KB每块
            std::vector<char> buffer(CHUNK_SIZE);
            while (file.read(buffer.data(), CHUNK_SIZE) || file.gcount() > 0)
            {
                size_t bytes_read = file.gcount();
                ctx_.conn->send_data(std::string(buffer.data(), bytes_read));
            }
            file.close();
            return;
        }
        else
        {
            ctx_.conn->send_data(generate_error_response(404));
            return;
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
    ctx_.conn->send_data(response);
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

ProcessResult HttpChannel::process(std::string &read_buffer)
{
    while (state_ != PARSE_DONE)
    {
        switch (state_)
        {
        case PARSE_REQUEST_LINE:
        {
            std::string line;
            if (!extract_line(read_buffer, line))
                return ProcessResult::CONTINUE;
            if (!parse_request_line(line))
            {

                return ProcessResult::CLOSE;
            }
            state_ = PARSE_HEADERS;
            break;
        }
        case PARSE_HEADERS:
        {
            std::string line;
            if (!extract_line(read_buffer, line))
                return ProcessResult::CONTINUE;
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
                ctx_.conn->send_data(error_reponse);
                return ProcessResult::CLOSE;
            }
            break;
        }
        case PARSE_BODY:
        {
            if (!parse_body(read_buffer))
                return ProcessResult::CONTINUE;
            break;
        }
        default:
            return ProcessResult::CLOSE;
        }
    }

    // 检测是否是对大模型的访问
    if (method_ == "POST" && path_ == "/v1/chat")
    {
        //  发送 SSE 握手头
        std::string header = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/event-stream\r\n"
                             "Cache-Control: no-cache\r\n"
                             "Connection: keep-alive\r\n"
                             "\r\n";
        ctx_.conn->send_data(header);

        // 解析 JSON 请求体
        try
        {
            json req = json::parse(body_);
            std::string session_id = req["session_id"];
            std::string message = req["message"];

            if (ctx_.llm_service)
            {
                ctx_.llm_service->handle_chat(session_id, message, ctx_);
            }
            else
            {
                ctx_.conn->send_data(generate_error_response(500));
            }
            return ProcessResult::CONTINUE;
        }
        catch (const std::exception &e)
        {
            ctx_.conn->send_data(generate_error_response(400));
            return ProcessResult::CLOSE;
        }
    }

    // 在生成普通响应之前，进行SSE检测
    if (method_ == "GET")
    {
        bool is_sse_path = (path_ == "/events");
        auto accept_it = headers_.find("accept");
        bool has_sse_accept = (accept_it != headers_.end() && accept_it->second.find("text/event-stream") != std::string::npos);
        if (is_sse_path || has_sse_accept)
        {
            reset();
            // std::cout << "SSE request detected, upgrading..." << std::endl;
            return ProcessResult::UPGRADE_SSE;
        }
    }

    // 生成并发送响应,由函数内部负责
    generate_response();

    // Keep-Alive 处理
    bool keep_alive = false;
    auto it = headers_.find("connection");
    if (it != headers_.end() && it->second == "keep-alive")
        keep_alive = true;

    if (keep_alive)
    {
        reset();
        // 返回继续处理的信号，由clientconnection循环处理调用
        return ProcessResult::CONTINUE;
    }
    else
    {
        return ProcessResult::CLOSE; // 关闭连接
    }
}