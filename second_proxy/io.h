#include <winsock2.h>

namespace IO {

    int CreateSocket();

    int Connect(int fd, const char *ip, int port);

} // namespace IO