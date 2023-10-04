#include <iostream>
#include <winsock2.h>

#include "io.h"

namespace params{
    const int N = 4;  // 线程池大小
    const int LOCAL_PROXY_PORT = 7890;  // 端口号
    const char *LOCAL_IP = "127.0.0.1";
    const int REMOTE_CONTROL_PORT = 12346;  // 控制端口号
    const char *REMOTE_CONTROL_IP = "0.0.0.0";
}

// 初始化Winsock库
int init() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "Failed to initialize Winsock\n");
        return -1;
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

    int ret = IO::Connect(fd, params::REMOTE_CONTROL_IP, params::REMOTE_CONTROL_PORT);
    if (ret < 0) {
        std::cout << "Failed to connect to remote control" << std::endl;
        // return -1;
    }
    std::cout << "Connected to remote control success." << std::endl;

    // 发送控制链接
    for (int i = 1; i<=40; i++) {
        auto wait_fd = IO::CreateSocket();
        if (wait_fd < 0) {
            std::cout << "Failed to create socket" << std::endl;
            break;
        }
        ret = IO::Connect(wait_fd, params::LOCAL_IP, params::LOCAL_PROXY_PORT);
        if (ret < 0) {
            std::cout << "Failed to connect to local proxy" << std::endl;
            break;
        }
        std::cout << "Connected to local proxy success, fd = " << wait_fd << std::endl;
    }
    

}