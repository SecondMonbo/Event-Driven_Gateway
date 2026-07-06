#ifndef EPOLL_LOOP_HPP
#define EPOLL_LOOP_HPP

#include <unordered_map>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include "ThreadPool.hpp"

class ClientConnection;

class EpollLoop
{
public:
    EpollLoop();
    ~EpollLoop();

    bool init(int port);
    void run();

    void add_write_event(int fd);    // 为实现写缓冲区提供，
    void remove_write_event(int fd); // 为链接注册EPOLLOUT;

    // 提交异步任务，回调在主线程执行
    template <typename Task, typename Callback>
    void submit_async(Task &&task, Callback &&callback)
    {
        thread_pool_.enqueue_with_callback(std::forward<Task>(task), std::forward<Callback>(callback));
    }

    ThreadPool &get_thread_pool() { return thread_pool_; }

    // 提供唤醒机制
    void wakeup();

private:
    void add_fd(int fd, uint32_t events);
    void remove_fd(int fd);
    void handle_accept();
    void handle_read(int fd);
    void remove_connection(int fd);

    int epfd_;
    int listen_fd_;
    int wakeup_fd_;
    std::unordered_map<int, std::shared_ptr<ClientConnection>> connections_;
    std::unordered_map<int, uint32_t> fd_events_;
    static const int MAX_EVENTS = 1024;

    ThreadPool thread_pool_;
};

#endif