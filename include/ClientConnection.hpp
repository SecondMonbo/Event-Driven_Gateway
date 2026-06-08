#ifndef CLIENT_CONNECTION_HPP
#define CLIENT_CONNECTION_HPP

#include <string>
#include <memory>

class HttpChannel;

class ClientConnection
{
public:
    ClientConnection(int fd, int id);
    ~ClientConnection();

    // 禁止拷贝
    ClientConnection(const ClientConnection &) = delete;
    ClientConnection &operator=(const ClientConnection &) = delete;

    // 允许移动（可选）
    ClientConnection(ClientConnection &&) = default;
    ClientConnection &operator=(ClientConnection &&) = default;

    int fd() const { return fd_; }
    int id() const { return conn_id_; }

    bool on_readable(); // 返回 true 表示连接需要关闭
    void send_response(const std::string &response);
    void close_connection();

private:
    int fd_;
    int conn_id_;
    std::string read_buffer_;
    std::unique_ptr<HttpChannel> http_channel_;
};

#endif