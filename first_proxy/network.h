#pragma once

#include <netinet/in.h>
#include <sys/epoll.h>

namespace Network {

class NetFunction {
public:
  static int Accept(int fd, struct sockaddr_in *addr);
  static int SetNonBlock(int fd);
  static void SetAddr(const char *pszIP, const unsigned short shPort,
                      struct sockaddr_in &addr);
  static int CreateEpoll();
  static int AddEpoll(int epoll_fd, int fd);
  static int DelEpoll(int epoll_fd, int fd);
  static int EpollWait(int epoll_fd, epoll_event *events, int max_events,
                       int timeout);

  static int CreateListenSocket(const char *pszIP, const unsigned short shPort,
                                bool bReuse = false);
  static int AcceptOneNonBlock(int fd, int &conn_fd);
};

int SetNonBlock(int fd);

int Connect(int fd, const char *pszIP, const unsigned short shPort);

int CreateConn(const std::string &host, const uint16_t port);

int SendUntilAll(int fd, const char *buf, int len);

} // namespace Network