#include "io.h"

namespace IO {

    int CreateSocket() {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return -1;
        }
        return fd;
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

} // namespace IO