#include "EpollLoop.hpp"
#include "ClientConnection.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <cerrno>

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

EpollLoop::EpollLoop() : epfd_(-1), listen_fd_(-1) {}

EpollLoop::~EpollLoop()
{
    if (epfd_ != -1)
        close(epfd_);
    if (listen_fd_ != -1)
        close(listen_fd_);
    for (auto &p : connections_)
        delete p.second;
    connections_.clear();
}

bool EpollLoop::init(int port)
{
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
    {
        perror("socket");
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(listen_fd_, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        return false;
    }

    if (listen(listen_fd_, SOMAXCONN) < 0)
    {
        perror("listen");
        return false;
    }

    if (set_nonblocking(listen_fd_) < 0)
    {
        perror("set_nonblocking");
        return false;
    }

    epfd_ = epoll_create1(0);
    if (epfd_ < 0)
    {
        perror("epoll_create1");
        return false;
    }

    add_fd(listen_fd_, EPOLLIN | EPOLLET);

    std::cout << "Server listening on port " << port << std::endl;
    return true;
}

void EpollLoop::add_fd(int fd, uint32_t events)
{
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) < 0)
    {
        perror("epoll_ctl add");
    }
}

void EpollLoop::remove_fd(int fd)
{
    epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
}

void EpollLoop::handle_accept()
{
    while (true)
    {
        sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, (sockaddr *)&client_addr, &len);
        if (client_fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            perror("accept");
            break;
        }

        if (set_nonblocking(client_fd) < 0)
        {
            close(client_fd);
            continue;
        }

        add_fd(client_fd, EPOLLIN | EPOLLET);

        static int next_id = 1;
        ClientConnection *conn = new ClientConnection(client_fd, next_id++);
        connections_[client_fd] = conn;

        std::cout << "New connection fd=" << client_fd << ", id=" << conn->id() << std::endl;
    }
}

void EpollLoop::handle_read(int fd)
{
    auto it = connections_.find(fd);
    if (it == connections_.end())
        return;

    ClientConnection *conn = it->second;
    bool need_delete = conn->on_readable();

    if (need_delete)
    {
        remove_connection(fd);
    }
}

void EpollLoop::remove_connection(int fd)
{
    auto it = connections_.find(fd);
    if (it != connections_.end())
    {
        delete it->second;
        connections_.erase(it);
        remove_fd(fd);
        // 注意：ClientConnection 析构时会 close(fd)
    }
}

void EpollLoop::run()
{
    epoll_event events[MAX_EVENTS];
    while (true)
    {
        int nfds = epoll_wait(epfd_, events, MAX_EVENTS, -1);
        if (nfds < 0)
        {
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < nfds; ++i)
        {
            int fd = events[i].data.fd;
            if (fd == listen_fd_)
            {
                handle_accept();
            }
            else if (events[i].events & EPOLLIN)
            {
                handle_read(fd);
            }
        }
    }
}