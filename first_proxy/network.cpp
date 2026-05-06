#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
#include <string.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "network.h"

namespace Network {

int NetFunction::Accept(int fd, struct sockaddr_in *addr) {
  socklen_t len = sizeof(struct sockaddr_in);
  return accept(fd, (struct sockaddr *)addr, &len);
}

int NetFunction::SetNonBlock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    return -1;
  }

  flags |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flags) == -1) {
    return -1;
  }

  return 0;
}

void NetFunction::SetAddr(const char *pszIP, const unsigned short shPort,
                          struct sockaddr_in &addr) {
  bzero(&addr, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(shPort);
  int nIP = 0;
  if (!pszIP || '\0' == *pszIP || 0 == strcmp(pszIP, "0") ||
      0 == strcmp(pszIP, "0.0.0.0") || 0 == strcmp(pszIP, "*")) {
    nIP = htonl(INADDR_ANY);
  } else {
    nIP = inet_addr(pszIP);
  }
  addr.sin_addr.s_addr = nIP;
}

int NetFunction::CreateEpoll() { return epoll_create1(0); }

int NetFunction::AddEpoll(int epoll_fd, int fd) {
  struct epoll_event ev;
  ev.data.fd = fd;
  ev.events = EPOLLIN;
  return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

int NetFunction::DelEpoll(int epoll_fd, int fd) {
  return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
}

int NetFunction::EpollWait(int epoll_fd, struct epoll_event *events,
                           int max_events, int timeout) {
  int n = epoll_wait(epoll_fd, events, max_events, timeout);
  if (n < 0) {
    if (errno == EINTR) {
      return 0;
    }
    return -1;
  }
  return n;
}

int NetFunction::CreateListenSocket(const char *pszIP,
                                    const unsigned short shPort, bool bReuse) {
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd >= 0) {
    if (shPort != 0) {
      if (bReuse) {
        int nReuseAddr = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &nReuseAddr,
                   sizeof(nReuseAddr));
      }
      struct sockaddr_in addr;
      SetAddr(pszIP, shPort, addr);
      int ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
      if (ret != 0) {
        close(fd);
        return -1;
      }
    }
  }
  return fd;
}

int NetFunction::AcceptOneNonBlock(int fd, int &conn_fd) {
  struct sockaddr_in addr;
  do {
    int _conn_fd = Accept(fd, &addr);
    if (_conn_fd >= 0) {
      if (SetNonBlock(_conn_fd) != 0) {
        return -1;
      }
      conn_fd = _conn_fd;
      return 0;
    } else {
      if ((errno == EAGAIN) || (errno == EINTR) || (errno == EWOULDBLOCK)) {
        continue;
      }
      return -2;
    }
  } while (1);

  return 0;
}

int Connect(int fd, const char *pszIP, const unsigned short shPort) {
  struct sockaddr_in addr;
  SetAddr(pszIP, shPort, addr);

  return connect(fd, (struct sockaddr *)&addr, sizeof(addr));
}

int CreateConn(const std::string &host, const uint16_t port) {
  // do connect
  int ret = 0;

  auto target_fd = IO::CreateTcpSocket();
  if (target_fd < 0) {
    std::cout << "CreateTcpSocket error: " << target_fd << std::endl;
    return -1;
  }

  // prevent connect timeout
  if (SetNonBlock(target_fd) != 0) {
    return -1;
  }

  hostent *hostinfo = gethostbyname(host.c_str());
  if (hostinfo == nullptr) {
    std::cout << "gethostbyname error: " << h_errno << std::endl;
    return -1;
  }

  ret = IO::Connect(target_fd, inet_ntoa(*(struct in_addr *)hostinfo->h_addr),
                    port);
  // 10ms timeout
  if (ret < 0) {
    if (errno == EINPROGRESS) {
      struct timeval tv;
      tv.tv_sec = 1;
      tv.tv_usec = 0;
      fd_set wset;
      FD_ZERO(&wset);
      FD_SET(target_fd, &wset);
      ret = select(target_fd + 1, nullptr, &wset, nullptr, &tv);
      if (ret == 0) {
        std::cout << "Connect timeout "
                  << " Host" << host << " Port" << port << std::endl;
        return -1;
      } else if (ret < 0) {
        std::cout << "Connect error: " << ret << strerror(errno) << std::endl;
        return -1;
      } else {
        int error = 0;
        socklen_t len = sizeof(error);
        ret = getsockopt(target_fd, SOL_SOCKET, SO_ERROR, &error, &len);
        if (ret != 0) {
          std::cout << "getsockopt error: " << ret << strerror(errno)
                    << std::endl;
          return -1;
        }
        if (error != 0) {
          std::cout << "Connect error: " << error << strerror(error)
                    << std::endl;
          return -1;
        }
      }
    } else {
      std::cout << "Connect error: " << ret << strerror(errno) << std::endl;
      return -1;
    }
  }

  // if (ret != 0) {
  //     std::cout << "Connect error: " << ret << strerror(errno) << std::endl;
  //     return -1;
  // }

  return target_fd;
}

int SendUntilAll(int fd, const char *buf, int len) {
  int n = 0;
  while (n < len) {
    int ret = send(fd, buf + n, len - n, 0);
    if (ret < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        continue;
      }
      return -1;
    }
    n += ret;
  }
  return n;
}

}; // namespace Network
