#ifndef CLIENT_CONNECTION_HPP
#define CLIENT_CONNECTION_HPP

#include <string>
#include <memory>
#include "core/EpollLoop.hpp"
#include "protocol/ProtocolHandler.hpp"
#include "core/ConnectionContext.hpp"

class ThreadPool;
class LLMService;

enum class ProtocolType
{
    UNKNOWN,
    HTTP,
    SSE,
    CUSTOM_LINE
};

class ClientConnection : public std::enable_shared_from_this<ClientConnection>
{
public:
    ClientConnection(int fd, int id, ThreadPool &tp, EpollLoop *const loop, LLMService &llm_service);
    ~ClientConnection();

    // 禁止拷贝
    ClientConnection(const ClientConnection &) = delete;
    ClientConnection &operator=(const ClientConnection &) = delete;

    // 在 make_shared 之后调用，初始化 ctx_
    void init_context();

    // 获取上下文 (返回引用，确保 ctx_ 已初始化)
    const ConnectionContext &get_context() const
    {
        return *ctx_;
    }

    int fd() const { return fd_; }
    int id() const { return conn_id_; }
    bool id_closed() const { return fd_ == -1; }

    bool on_readable(); // 返回 true 表示连接需要关闭
    void close_connection();

    // 获取读缓冲区引用（协议检测用）
    std::string &read_buffer() { return read_buffer_; }

    // 实现非阻塞写
    void send_data(const std::string &data); // 由协议处理器调用
    void handle_write();                     // 由EpollLoop在EPOLLOUT时调用
    void register_write_event();             // 注册写事件

    // 唤醒主循环
    void flush()
    {
        if (handler_)
            handler_->process(read_buffer_);
    };
    void wakeup() { loop_->wakeup(); };

private:
    // C++按照声明顺序初始化！！！
    int fd_;
    int conn_id_;
    ThreadPool &thread_pool_;
    EpollLoop *const loop_;
    LLMService &llm_service_;

    std::string read_buffer_;
    std::string write_buffer_;

    bool write_registered_ = false; // 是否已注册EPOLLOUT事件
    ProtocolType protocol_type_ = ProtocolType::UNKNOWN;

    // 调整初始化顺序，先上下文，后协议，是为了析构时，使用上下文的协议会先析构避免悬垂
    std::unique_ptr<ConnectionContext> ctx_; // 改为独占指针，确保协议层只有使用权，同时延迟初始化
    std::unique_ptr<ProtocolHandler> handler_;
};

#endif