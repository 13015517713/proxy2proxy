#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <netdb.h>
#include <iostream>
#include <sys/epoll.h>
#include <fcntl.h>

#include "io.h"

namespace IO {

    static void SetAddr(const char *pszIP, const unsigned short shPort, struct sockaddr_in &addr)
    {
        bzero(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(shPort);
        int nIP = 0;
        if (!pszIP || '\0' == *pszIP || 0 == strcmp(pszIP, "0") || 0 == strcmp(pszIP, "0.0.0.0") || 0 == strcmp(pszIP, "*"))
        {
            nIP = htonl(INADDR_ANY);
        }
        else
        {
            nIP = inet_addr(pszIP);
        }
        addr.sin_addr.s_addr = nIP;
    }

    int CreateTcpSocket(const char *pszIP /* = "*" */, const unsigned short shPort /* = 0 */, bool bReuse /* = false */)
    {
        int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd >= 0)
        {
            if (shPort != 0)
            {
                if (bReuse)
                {
                    int nReuseAddr = 1;
                    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &nReuseAddr, sizeof(nReuseAddr));
                }
                struct sockaddr_in addr;
                SetAddr(pszIP, shPort, addr);
                int ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
                if (ret != 0)
                {
                    close(fd);
                    return -1;
                }
            }
        }
        return fd;
    }

    int Accept(int fd, struct sockaddr_in *addr)
    {
        socklen_t len = sizeof(struct sockaddr_in);
        return accept(fd, (struct sockaddr *)addr, &len);
    }

    int Connect(int fd, const char *pszIP, const unsigned short shPort)
    {
        struct sockaddr_in addr;
        SetAddr(pszIP, shPort, addr);
        return connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    }

    int CreateConn(const std::string& host, const uint16_t port) {
        // do connect
        int ret = 0;
        
        auto target_fd = IO::CreateTcpSocket();
        if (target_fd < 0) {
            std::cout << "CreateTcpSocket error: " << target_fd << std::endl;
            return -1;
        }

        hostent *hostinfo = gethostbyname(host.c_str());
        if (hostinfo == nullptr) {
            std::cout << "gethostbyname error: " << h_errno << std::endl;
            return -1;
        }

        ret = IO::Connect(target_fd, inet_ntoa(*(struct in_addr*)hostinfo->h_addr), port);
        if (ret != 0) {
            std::cout << "Connect error: " << ret << std::endl;
            return -1;
        }

        return target_fd;
    }

    int CreateEpoll() {
        int fd = epoll_create1(0);
        // if (fd < 0) {
        //     return -1;
        // }

        // int flags = fcntl(fd, F_GETFL, 0);
        // if (flags == -1) {
        //     return -1;
        // }

        // flags |= O_NONBLOCK;
        // if (fcntl(fd, F_SETFL, flags) == -1) {
        //     return -1;
        // }

        return fd;
    }

    int AddEpoll(int epoll_fd, int fd) {
        struct epoll_event ev;
        ev.data.fd = fd;
        ev.events = EPOLLIN; // 水平触发
        return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    }

    int DelEpoll(int epoll_fd, int fd) {
        return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    }

    // 更上层的封装epoll
    int EpollWait(int epoll_fd, struct epoll_event *events, int max_events, int timeout) {
        int n = epoll_wait(epoll_fd, events, max_events, timeout);
        if (n < 0) {
            if (errno == EINTR) {
                return 0;
            }
            return -1;
        }
        return n;
    }

    int SendUntilAll(int fd, const char *buf, int len) {
        int n = 0;
        while (n < len) {
            int ret = send(fd, buf + n, len - n, 0);
            if (ret < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    continue;
                }
                return -1;
            }
            n += ret;
        }
        return n;
    }

}; // namespace IO

