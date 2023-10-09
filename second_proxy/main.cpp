#include <iostream>
#include <unistd.h>
#include <winsock2.h>
#include <assert.h>

#include "io.h"
#include "threadpool.hpp"
namespace params{
    const int N = 4;  // 线程池大小
    const char *LOCAL_IP = "127.0.0.1";
    const int LOCAL_PROXY_PORT = 7890;  // 端口号
    const char *REMOTE_IP = "172.24.0.100";
    // const char *REMOTE_IP = "47.115.216.30";
    const int REMOTE_CONTROL_PORT = 12346;  // 控制端口号
    const int REMOTE_PROXY_IP = 12347; // 代理端口号
    const int PRE_SOCKET_LINKCNT = 128; // 预先申请的socket link数量
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

static inline int CreateRemoteProxySocket() {
    int fd = IO::CreateSocket();
    if (fd < 0) {
        std::cout << "Failed to create socket" << std::endl;
        return -1;
    }
    int ret = IO::Connect(fd, params::REMOTE_IP, params::REMOTE_PROXY_IP);
    if (ret < 0) {
        std::cout << "Failed to connect to remote control, " << strerror(errno) << std::endl;
        return -1;
    }
    return fd;
}

static inline int CreateLocalProxySocket() {
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

int InitSockLink(EventLoop::EventLoop& loop) {
    int local_fd, remote_fd;
    for (int i = 0; i < 128; ++i) {
        int ret = CreateSocketLink(local_fd, remote_fd);
        if (ret < 0) {
            std::cout << "Failed to create socket link" << std::endl;
            exit(-1);
        }
        loop.AddSocketLink(local_fd, remote_fd);
    }
    std::cout << "create socket link pool success" << std::endl;
    return 0;
}

static inline uint16_t NeedSockLinkCnt(const uint16_t had_cnt, const uint16_t need_cnt) {
    const uint16_t step = 50;
    if (had_cnt >= need_cnt + step) {
        return 0;
    }
    // auto diff = need_cnt - had_cnt;
    // return diff * 2;
    return step;
}

// 主线程，接收控制链接，收到一个就申请一个
int AcceptFdReq(int fd, EventLoop::EventLoop& loop) {
    std::cout << "Start to accept fd req" << std::endl;
    static uint16_t had_cnt = params::PRE_SOCKET_LINKCNT;
    static uint16_t all_need_cnt = 0;

    int n = 0;
    int local_fd, remote_fd;
    char buf[1024];
    while (1) {
        n = recv(fd, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                continue;
            }
            std::cout << "Failed to read from control proxy " << strerror(errno) << std::endl;
            return -1;
        } else if (n == 0) {
            return 0;
        } else {
            all_need_cnt += n;
            uint16_t need_cnt = NeedSockLinkCnt(had_cnt, all_need_cnt + n);
            // std::cout << "accept sock link req, has cnt: " << had_cnt << " all_need_cnt: " << all_need_cnt << std::endl;
            for (int i=0; i<need_cnt; ++i) {
                int ret = CreateSocketLink(local_fd, remote_fd);
                if (ret < 0) {
                    std::cout << "Failed to create socket link" << std::endl;
                    exit(-1);
                }
                std::cout << "Create socket link " << local_fd << " " << remote_fd << std::endl;
                loop.AddSocketLink(local_fd, remote_fd);
            }
            // std::cout << "create sock link req, has cnt: " << need_cnt << std::endl;
            had_cnt += need_cnt;
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
    int ret = IO::Connect(fd, params::REMOTE_IP, params::REMOTE_CONTROL_PORT);
    if (ret < 0) {
        std::cout << "Failed to connect to local proxy" << std::endl;
        return -1;
    }
    std::cout << "Connect to remote control success." << std::endl;
    ret = IO::SetNonBlocking(fd);
    if (ret < 0) {
        std::cout << "Failed to set non blocking" << std::endl;
        return -1;
    }

    EventLoop::EventLoop loop(params::N);
    loop.spawn();

    // 可以预先申请pre_N个socket link
    ret = InitSockLink(loop);
    assert(ret == 0);
    ret = AcceptFdReq(fd, loop);
    assert(ret == 0);
    
    WSACleanup();

}