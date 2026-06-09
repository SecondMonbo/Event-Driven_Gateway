#ifndef EPOLL_LOOP_HPP
#define EPOLL_LOOP_HPP

#include <map>
#include <sys/epoll.h>
#include "ThreadPool.hpp"

class ClientConnection;

class EpollLoop
{
public:
    EpollLoop();
    ~EpollLoop();

    bool init(int port);
    void run();

private:
    void add_fd(int fd, uint32_t events);
    void remove_fd(int fd);
    void handle_accept();
    void handle_read(int fd);
    void remove_connection(int fd);

    int epfd_;
    int listen_fd_;
    std::map<int, ClientConnection *> connections_;
    static const int MAX_EVENTS = 1024;

    ThreadPool thread_pool_;
};

#endif