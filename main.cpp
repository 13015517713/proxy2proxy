#include <iostream>
#include <boost/asio.hpp>

#include "io.h"
#include "threadpool.hpp"

using namespace boost::asio;
using ip::tcp;

namespace params{
    const int N = 4;  // 线程池大小
    const int PORT = 12345;  // 端口号
    const int CONTROL_PORT = 12346;  // 控制端口号
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

// 监听控制端口
int AcceptControlLoop(int fd, ThreadPool::ThreadPool& tp) {
    while (true) {
        struct sockaddr_in addr;
        int connfd = IO::Accept(fd, &addr);
        if (connfd >= 0) {
            if (IO::SetNonBlock(connfd) != 0) {
                std::cout << "SetNonBlock error" << std::endl;
                continue;
            }
            std::cout << "Accept " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << " fd: " << connfd << std::endl;
            tp.submit_socket_link(connfd);
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

int main() {
    signal(SIGPIPE, SIG_IGN);

    int accept_fd = IO::CreateTcpSocket(params::IP, params::PORT, true);
    if (accept_fd < 0) {
        std::cout << "CreateTcpSocket failed" << std::endl;
        return -1;
    }
    if (listen(accept_fd, 1024) != 0) {
        std::cout << "listen failed" << std::endl;
        return -1;
    }

    ThreadPool::ThreadPool tp(params::N);
    tp.spawn();

    int control_fd = IO::CreateTcpSocket(params::IP, params::CONTROL_PORT, true);
    if (control_fd < 0) {
        std::cout << "Create Control TcpSocket failed" << std::endl;
        return -1;
    }
    if (listen(control_fd, 1024) != 0) {
        std::cout << "listen failed" << std::endl;
        return -1;
    }
    
    std::thread control_thread(AcceptControlLoop, control_fd, std::ref(tp));
    AcceptLoop(control_fd, tp);
    control_thread.join();

    return 0;
}
