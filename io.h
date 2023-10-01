#pragma once

#include <netinet/in.h>

namespace IO {
    // 创建socket: 非阻塞
    int CreateTcpSocket(const char *pszIP = nullptr, const unsigned short shPort = 0, bool bReuse = false);

    // accept获取链接
    int Accept(int fd, struct sockaddr_in *addr);

    int Connect(int fd, const char *pszIP, const unsigned short shPort);

    int CreateConn(const std::string& host, const uint16_t port);

    int CreateEpoll();

    int AddEpoll(int epoll_fd, int fd);

     int EpollWait(int epoll_fd, struct epoll_event *events, int max_events, int timeout);
}