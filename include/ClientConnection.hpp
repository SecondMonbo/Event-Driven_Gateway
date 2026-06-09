#ifndef CLIENT_CONNECTION_HPP
#define CLIENT_CONNECTION_HPP

#include <string>
#include <memory>

class HttpChannel;
class ThreadPool;

class ClientConnection : public std::enable_shared_from_this<ClientConnection>
{
public:
    ClientConnection(int fd, int id, ThreadPool &tp);
    ~ClientConnection();

    // 禁止拷贝
    ClientConnection(const ClientConnection &) = delete;
    ClientConnection &operator=(const ClientConnection &) = delete;

    int fd() const { return fd_; }
    int id() const { return conn_id_; }
    bool id_closed() const { return fd_ == -1; }

    bool on_readable(); // 返回 true 表示连接需要关闭
    void send_response(const std::string &response);
    void close_connection();

    // 获取读缓冲区引用（协议检测用）
    std::string &read_buffer() { return read_buffer_; }

private:
    int fd_;
    int conn_id_;
    std::string read_buffer_;
    std::unique_ptr<HttpChannel> http_channel_;
    ThreadPool &thread_pool_;
};

#endif