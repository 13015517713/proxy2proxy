#include <iostream>
#include <boost/asio.hpp>

#include "io.h"
#include "threadpool.hpp"

using namespace boost::asio;
using ip::tcp;

namespace params{
    const int N = 4;  // 线程池大小
    const int PORT = 12345;  // 端口号
    const char *IP = "0.0.0.0";
}

int AcceptLoop(int fd, ThreadPool::ThreadPool& tp) {
    while (true) {
        struct sockaddr_in addr;
        int connfd = IO::Accept(fd, &addr);
        if (connfd >= 0) {
            std::cout << "Accept " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << std::endl;
            // 放到全局队列里
            tp.submit(connfd);
        } else {
            // 出现问题
            if ((errno == EAGAIN) || (errno == EINTR) || (errno == EWOULDBLOCK)) {
                continue;
            }
            // return -1; // 出现问题;
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

    AcceptLoop(accept_fd, tp);

    // 线程池大小

    // accept() {  // 主线程accept socket
    //     socket s{io_service};
    //     acceptor a{io_service, ip::tcp::endpoint{ip::tcp::v4(), 12345}};
    //     a.accept(s);
    //     tp.submit([s] {  // 线程池处理socket
    //         // ...
    //     });
    // }    

    return 0;
}
