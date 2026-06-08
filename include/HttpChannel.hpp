#ifndef HTTP_CHANNEL_HPP
#define HTTP_CHANNEL_HPP

#include <string>
#include <unordered_map>

class HttpChannel
{
public:
    bool process(int client_fd, std::string &read_buffer);

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

    bool extract_line(std::string &buffer, std::string &line);
    bool parse_request_line(const std::string &line);
    bool parse_header_line(const std::string &line);
    bool parse_body(std::string &buffer);
    std::string generate_response();
    void send_error_response(int fd, int status_code);
    void reset();
};

#endif