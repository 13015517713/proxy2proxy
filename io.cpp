#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

#include "io.h"

namespace IO {

    static void SetAddr(const char *pszIP, const unsigned short shPort, struct sockaddr_in &addr)
    {
        bzero(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(shPort);
        int nIP = 0;
        if (!pszIP || '\0' == *pszIP || 0 == strcmp(pszIP, "0") || 0 == strcmp(pszIP, "0.0.0.0") || 0 == strcmp(pszIP, "*"))
        {
            nIP = htonl(INADDR_ANY);
        }
        else
        {
            nIP = inet_addr(pszIP);
        }
        addr.sin_addr.s_addr = nIP;
    }

    int CreateTcpSocket(const char *pszIP /* = "*" */, const unsigned short shPort /* = 0 */, bool bReuse /* = false */)
    {
        int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd >= 0)
        {
            if (shPort != 0)
            {
                if (bReuse)
                {
                    int nReuseAddr = 1;
                    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &nReuseAddr, sizeof(nReuseAddr));
                }
                struct sockaddr_in addr;
                SetAddr(pszIP, shPort, addr);
                int ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
                if (ret != 0)
                {
                    close(fd);
                    return -1;
                }
            }
        }
        return fd;
    }

    int Accept(int fd, struct sockaddr_in *addr)
    {
        socklen_t len = sizeof(struct sockaddr_in);
        return accept(fd, (struct sockaddr *)addr, &len);
    }

}; // namespace IO

