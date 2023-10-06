#pragma once

#include <winsock2.h>

extern "C" {
    #include "wepoll.h"
}

namespace IO {
    

    int CreateSocket();

    int SetNonBlocking(int fd);

    int Connect(int fd, const char *ip, int port);

    int Select(fd_set& readfds, const int max_fd, const timeval& timeout);

    int SendUntilAll(int fd, const char *buf, int len);

    HANDLE EpollCreate();

    int AddEpoll(HANDLE epoll_fd, int fd);

    int DelEpoll(HANDLE epoll_fd, int fd);

    int EpollWait(HANDLE epoll_fd, epoll_event *events, int max_events, int timeout);

} // namespace IO