#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "io.h"
#include "threadpool.hpp"

namespace params{
    const int N = 20;  // 线程池大小
    const int PROXY_PORT = 12345;  // 端口号
    const int CONTROL_PORT = 12346;  // 控制端口号
    const int SOCKLINK_PORT = 12347;  // 控制端口号
    const char *IP = "0.0.0.0";
}

int AcceptLoop(int fd, ThreadPool::ThreadPool& tp) {
    while (true) {
        struct sockaddr_in addr;
        int connfd = IO::Accept(fd, &addr);
        if (connfd >= 0) {
            if (IO::SetNonBlock(connfd) != 0) {
                std::cout << "SetNonBlock error" << std::endl;
                continue;
            }
            std::cout << "Accept " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << " fd: " << connfd << std::endl;
            // 放到全局队列里
            tp.submit(connfd);
        } else {
            if ((errno == EAGAIN) || (errno == EINTR) || (errno == EWOULDBLOCK)) {
                continue;
            }
            std::cout << "Accept error but ignore." << std::endl;
            continue; // 暂时忽略问题
        }
    }
    return 0;
}

// 连接一次即可
int AcceptControlLoop(int fd, int& conn_fd) {
    struct sockaddr_in addr;
    do {
        int connfd = IO::Accept(fd, &addr);
        if (connfd >= 0) {
            if (IO::SetNonBlock(connfd) != 0) {
                std::cout << "SetNonBlock error" << std::endl;
                continue;
            }
            conn_fd = connfd;
            std::cout << "AcceptControl " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << " fd: " << connfd << std::endl;
            return 0;
        } else {
            if ((errno == EAGAIN) || (errno == EINTR) || (errno == EWOULDBLOCK)) {
                continue;
            }
            return -1;
        }
    }while(1);
    
    return 0;
}

int AcceptSocklinkLoop(int fd, ThreadPool::ThreadPool& tp) {
    struct sockaddr_in addr;
    while (true) {
        int connfd = IO::Accept(fd, NULL);
        if (connfd >= 0) {
            if (IO::SetNonBlock(connfd) != 0) {
                std::cout << "SetNonBlock error" << std::endl;
                continue;
            }
            // 放到全局队列可用队列里
            tp.submit_socket_link(connfd);
        } else {
            if ((errno == EAGAIN) || (errno == EINTR) || (errno == EWOULDBLOCK)) {
                continue;
            }
            return -1;
        }
    }
    return 0;
}

int main() {
    signal(SIGPIPE, SIG_IGN);
 
    int accept_fd = IO::CreateTcpSocket(params::IP, params::PROXY_PORT, true);
    if (accept_fd < 0) {
        std::cout << "CreateTcpSocket failed" << std::endl;
        return -1;
    }
    if (listen(accept_fd, 1024) != 0) {
        std::cout << "listen failed" << std::endl;
        return -1;
    }


    int control_fd = IO::CreateTcpSocket(params::IP, params::CONTROL_PORT, true); // 用来申请socket_link
    int socklink_fd = IO::CreateTcpSocket(params::IP, params::SOCKLINK_PORT, true); // 用来接收socket_link
    if (control_fd < 0 || socklink_fd < 0) {
        std::cout << "Create Control TcpSocket failed" << std::endl;
        return -1;
    }
    if (listen(control_fd, 1024) != 0 || listen(socklink_fd, 1024) != 0) {
        std::cout << "listen failed" << std::endl;
        return -1;
    }

    int control_conn_fd = -1;
    int ret = AcceptControlLoop(control_fd, control_conn_fd);
    if (ret != 0) {
        std::cout << "AcceptControlLoop failed" << std::endl;
        return -1;
    }

    ThreadPool::ThreadPool tp(params::N);
    tp.control_fd_ = control_conn_fd;
    tp.spawn();

    std::thread socklink_thread(AcceptSocklinkLoop, socklink_fd, std::ref(tp));
    AcceptLoop(accept_fd, tp);
    socklink_thread.join();

    return 0;
}
