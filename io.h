#pragma once

#include <netinet/in.h>

namespace IO {
    // 创建socket: 非阻塞
    int CreateTcpSocket(const char *pszIP /* = "*" */, const unsigned short shPort /* = 0 */, bool bReuse /* = false */);

    // accept获取链接
    int Accept(int fd, struct sockaddr_in *addr);
}