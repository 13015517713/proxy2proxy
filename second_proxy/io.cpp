#include <iostream>

#include "io.h"

namespace IO {

    int CreateSocket() {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return -1;
        }
        return fd;
    }

    int SetNonBlocking(int fd) {
        unsigned long ul = 1;
        int ret = ioctlsocket(fd, FIONBIO, (unsigned long *) &ul);
        if (ret < 0) {
            return -1;
        }
        return 0;
    }

    int Connect(int fd, const char *ip, int port) {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.S_un.S_addr = inet_addr(ip);
        int ret = connect(fd, (struct sockaddr *) &addr, sizeof(addr));
        if (ret < 0) {
            return -1;
        }
        return 0;
    }

    int CreateReadableFdSet(fd_set& readfds, fd_set& writefds, int& max_fd, int fd) {
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        FD_SET(fd, &writefds);
        max_fd = fd;
        return 0;
    }

    int Select(fd_set& readfds, const int max_fd, const timeval& timeout) {
        int ret = select(max_fd + 1, &readfds, nullptr, nullptr, &timeout);
        if (ret < 0) {
            std::cout << "Failed to select: " << strerror(errno) << std::endl;
            return -1;
        }
        return 0;
    }

    int SendUntilAll(int fd, const char *buf, int len) {
        int ret = 0;
        int total = 0;
        while (total < len) {
            ret = send(fd, buf + total, len - total, 0);
            if (ret < 0) {
                return -1;
            }
            total += ret;
        }
        return total;
    }

    HANDLE EpollCreate() {
        HANDLE epoll_fd = epoll_create(1024);
        return epoll_fd;
    }

    int AddEpoll(HANDLE epoll_fd, int fd) {
        epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = fd;
        int ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
        if (ret < 0) {
            return -1;
        }
        return 0;
    }

    int DelEpoll(HANDLE epoll_fd, int fd) {
        int ret = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
        if (ret < 0) {
            return -1;
        }
        return 0;
    }

    int EpollWait(HANDLE epoll_fd, epoll_event *events, int max_events, int timeout) {
        int ret = epoll_wait(epoll_fd, events, max_events, timeout);
        if (ret < 0) {
            return -1;
        }
        return ret;
    }


} // namespace IO