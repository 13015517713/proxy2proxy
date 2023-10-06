#pragma once

#include <winsock2.h>

namespace IO {

    int CreateSocket();

    int Connect(int fd, const char *ip, int port);

    int Select(fd_set& readfds, const int max_fd, const timeval& timeout);

    int SendUntilAall(int fd, const char *buf, int len);

} // namespace IO