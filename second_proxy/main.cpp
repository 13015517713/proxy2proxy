#include <iostream>
#include <unistd.h>
#include <winsock2.h>
#include <assert.h>

#include "io.h"
#include "threadpool.hpp"

namespace params{
    const int N = 4;  // 线程池大小
    const int LOCAL_PROXY_PORT = 7890;  // 端口号
    const char *LOCAL_IP = "127.0.0.1";
    const int REMOTE_CONTROL_PORT = 12346;  // 控制端口号
    const char *REMOTE_CONTROL_IP = "0.0.0.0";
}

// 初始化Winsock库
static int init() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "Failed to initialize Winsock\n");
        return -1;
    }
    return 0;
}

static inline int CreateLocalProxySocket() {
    int fd = IO::CreateSocket();
    if (fd < 0) {
        std::cout << "Failed to create socket" << std::endl;
        return -1;
    }
    int ret = IO::Connect(fd, params::REMOTE_CONTROL_IP, params::REMOTE_CONTROL_PORT);
    if (ret < 0) {
        std::cout << "Failed to connect to remote control" << std::endl;
        return -1;
    }
    return fd;
}

static inline int CreateRemoteProxySocket() {
    int fd = IO::CreateSocket();
    if (fd < 0) {
        std::cout << "Failed to create socket" << std::endl;
        return -1;
    }
    int ret = IO::Connect(fd, params::LOCAL_IP, params::LOCAL_PROXY_PORT);
    if (ret < 0) {
        std::cout << "Failed to connect to local proxy" << std::endl;
        return -1;
    }
    return fd;
}

static inline int CreateSocketLink(int& local_fd, int& remote_fd) {
    local_fd = CreateLocalProxySocket();
    if (local_fd < 0) {
        std::cout << "Failed to create local proxy socket" << std::endl;
        return -1;
    }
    remote_fd = CreateRemoteProxySocket();
    if (remote_fd < 0) {
        close(local_fd);
        std::cout << "Failed to create remote socket" << std::endl;
        return -1;
    }
    return 0;
}

// 主线程，接收控制链接，收到一个就申请一个
int AcceptFdReq(int fd, EventLoop::EventLoop& loop) {
    std::cout << "Start to accept fd req" << std::endl;
    int n = 0;
    while (1) {
        n = read(fd, NULL, 1);
        continue;
        if (n < 0) {
            std::cout << "Failed to read from local proxy" << std::endl;
            return -1;
        } else if (n == 0) {
            std::cout << "Local proxy closed" << std::endl;
            return 0;
        } else {
            int local_fd, remote_fd;
            int ret = CreateSocketLink(local_fd, remote_fd);
            if (ret < 0) {
                std::cout << "Failed to create socket link" << std::endl;
                // return -1;
                continue;
            }
            loop.AddSocketLink(local_fd, remote_fd);
        }
    }
    return 0;
}

int main() {
    init();

    int fd = IO::CreateSocket();
    if (fd < 0) {
        std::cout << "Failed to create socket" << std::endl;
        return -1;
    }
    int ret = IO::Connect(fd, params::LOCAL_IP, params::LOCAL_PROXY_PORT);
    if (ret < 0) {
        std::cout << "Failed to connect to local proxy" << std::endl;
        return -1;
    }

    EventLoop::EventLoop loop(1);
    loop.spawn();

    ret = AcceptFdReq(fd, loop);
    assert(ret != 0);
    
}