#include <sstream>
#include <iostream>
#include <cctype>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fstream>
#include <limits.h>
#include <filesystem>

#include "protocol/HttpChannel.hpp"
#include "third_party/json.hpp"
#include "llm/LLMService.hpp"

namespace // 纯工具函数
{
    bool is_with_root(const std::filesystem::path &root, const std::filesystem::path &file)
    {
        std::string r = root.string();
        std::string f = file.string();
        return f == r || f.rfind(r + "/", 0) == 0;
    }
    std::string reason_phrase(int status_code)
    {
        std::string status_text;
        switch (status_code)
        {
        case 200:
            status_text = "OK";
            break;
        case 400:
            status_text = "Bad Request";
            break;
        case 403:
            status_text = "Forbidden";
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
        return status_text;
    };
}

using json = nlohmann::json;

HttpChannel::HttpChannel(const ConnectionContext &ctx) : ctx_(ctx)
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

    auto q = path_.find("?");
    if (q != std::string::npos)
    {
        query_ = path_.substr(q + 1);
        path_.erase(q);
    }

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

    if (lower_key == "connection")
    {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c)
                       { return std::tolower(c); });

        if (v.find("close") != std::string::npos)
            keep_alive_ = false;
        else if (v.find("keep-alive") != std::string::npos)
            keep_alive_ = true;
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

ProcessResult HttpChannel::handle_get()
{
    // 检测 /events 或 Accept: text/event-stream -> 升级
    bool is_events = (path_ == "/events");
    auto it = headers_.find("accept");
    bool wants_sse = (it != headers_.end()) && (it->second.find("text/event-stream")) != std::string::npos;

    if (is_events || wants_sse)
    {
        reset();
        return ProcessResult::UPGRADE_SSE;
    }

    // 静态文件
    bool ok = serve_static_file(path_ == "/" ? "index.html" : path_);
    if (!ok)
        return ProcessResult::CLOSE;

    if (!keep_alive_)
        return ProcessResult::CLOSE;

    reset();
    return ProcessResult::CONTINUE;
}

ProcessResult HttpChannel::handle_post()
{
    if (path_ == "/v1/chat")
    {
        try
        {
            json req = json::parse(body_);
            ctx_.pending_llm.session_id = req.at("session_id").get<std::string>();
            ctx_.pending_llm.message = req.at("message").get<std::string>();
            ctx_.pending_llm.valid = true;

            reset();
            return ProcessResult::UPGRADE_SSE;
        }
        catch (...)
        {
            send_error_response(400);
            return ProcessResult::CLOSE;
        }
    }

    if (path_ == "/")
    {
        if (!send_simple_response(200, "text/plain", "Receive body: " + body_))
            return ProcessResult::CLOSE;

        if (!keep_alive_)
            return ProcessResult::CLOSE;

        reset();
        return ProcessResult::CONTINUE;
    }

    return handle_no_allowed();
}

ProcessResult HttpChannel::handle_no_allowed()
{
    std::string body = "<h1>Method Not Allowed</h1>";
    std::string resp = "HTTP/1.1 405 Method Not Allowed\r\n"
                       "Allow: GET, POST\r\n"
                       "Content-Type: text/html\r\n"
                       "Content-Length: " +
                       std::to_string(body.size()) + "\r\n"
                                                     "Connection: close\r\n"
                                                     "\r\n" +
                       body;

    ctx_.send_data(resp);
    return ProcessResult::CLOSE;
}

bool HttpChannel::serve_static_file(const std::string &rel_path)
{
    namespace fs = std::filesystem;

    fs::path www_root = fs::path(get_executable_dir()) / ".." / "www";
    std::error_code ec;
    fs::path root = fs::weakly_canonical(www_root, ec);
    if (ec)
        return send_error_response(500);

    std::string rel = rel_path;
    while (!rel.empty() && rel.front() == '/')
    {
        rel.erase(rel.begin());
    }
    if (rel.empty())
        rel = "index.html";

    fs::path file = fs::weakly_canonical(www_root / rel, ec);
    if (ec)
        return send_error_response(400);

    if (!is_with_root(root, file))
    {
        return send_error_response(403);
    }

    struct stat st;
    if (stat(file.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        return send_error_response(404);

    std::ifstream in(file, std::ios::binary);
    if (!in)
        return send_error_response(404);

    std::string header = "HTTP/1.1 200 OK\r\n"
                         "Content-Type: " +
                         get_mime_type(file.string()) + "\r\n"
                                                        "Content-Length: " +
                         std::to_string(st.st_size) + "\r\n"
                                                      "connection: " +
                         (keep_alive_ ? "keep-alive" : "close") + "\r\n"
                                                                  "\r\n";

    if (!ctx_.send_data(header))
        return false;

    std::vector<char> buf(64 * 1024);
    while (in.read(buf.data(), buf.size()) || in.gcount() > 0)
    {
        if (!ctx_.send_data(std::string(buf.data(), in.gcount())))
            return false;
    }

    return true;
}

bool HttpChannel::send_error_response(int status)
{
    keep_alive_ = false;
    return ctx_.send_data(generate_error_response(status));
}

bool HttpChannel::send_simple_response(int status, const std::string &content_type, const std::string &body)
{
    std::string resp = "HTTP/1.1 " + std::to_string(status) + " " +
                       reason_phrase(status) + "\r\n"
                                               "Content-Type: " +
                       content_type + "\r\n"
                                      "Content-Length: " +
                       std::to_string(body.size()) + "\r\n"
                                                     "Connection: " +
                       (keep_alive_ ? "keep-alive" : "close") + "\r\n"
                                                                "\r\n" +
                       body;

    return ctx_.send_data(resp);
}

std::string HttpChannel::generate_error_response(int status_code)
{
    std::string status_text = reason_phrase(status_code);
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

void HttpChannel::reset()
{
    state_ = PARSE_REQUEST_LINE;
    method_.clear();
    path_.clear();
    version_.clear();
    headers_.clear();
    body_.clear();
    content_length_ = 0;
    keep_alive_ = true;
    query_.clear();
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
                ctx_.send_data(error_reponse);
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

    if (method_ == "GET")
        return handle_get();
    if (method_ == "POST")
        return handle_post();

    return handle_no_allowed();
}