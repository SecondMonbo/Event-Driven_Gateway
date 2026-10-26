#ifndef HTTP_CHANNEL_HPP
#define HTTP_CHANNEL_HPP

#include <string>
#include <unordered_map>
#include <memory>
#include "ProtocolHandler.hpp"
#include "core/ClientConnection.hpp"

class ClientConnection;
class ThreadPool;

class HttpChannel : public ProtocolHandler
{
public:
    HttpChannel(const ConnectionContext &ctx); // 线程池集成新增
    ~HttpChannel() = default;

    ProcessResult process(std::string &read_buffer) override;
    void reset() override;

private:
    enum ParseState
    {
        PARSE_REQUEST_LINE,
        PARSE_HEADERS,
        PARSE_BODY,
        PARSE_DONE
    };
    ParseState state_ = PARSE_REQUEST_LINE;

    std::string method_;
    std::string path_;
    std::string version_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    int content_length_ = 0;

    const ConnectionContext &ctx_;

    bool extract_line(std::string &buffer, std::string &line);
    bool parse_request_line(const std::string &line);
    bool parse_header_line(const std::string &line);
    bool parse_body(std::string &buffer);
    bool generate_response();
    std::string generate_error_response(int status_code);

    std::string get_mime_type(const std::string &path);
    std::string get_executable_dir();

    // 方法相关，后续规模扩大可考虑函数表映射
    ProcessResult handle_get();
    ProcessResult handle_post();
    ProcessResult handle_no_allowed();

    bool serve_static_file(const std::string &rel_path);
    bool send_error_response(int status);
    bool send_simple_response(int status, const std::string &content_type, const std::string &body);

    bool keep_alive_ = true;
    std::string  query_;
};

#endif