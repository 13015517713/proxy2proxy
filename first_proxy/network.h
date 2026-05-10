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
  // 在已经 AddEpoll 过的 fd 上, 开启写关注 (关注集 EPOLLIN | EPOLLOUT)
  static int EnableEpollWrite(int epoll_fd, int fd);
  // 在已经 AddEpoll 过的 fd 上, 关闭写关注 (关注集回到只 EPOLLIN)
  static int DisableEpollWrite(int epoll_fd, int fd);
  static int EpollWait(int epoll_fd, epoll_event *events, int max_events,
                       int timeout);

  static int CreateListenSocket(const char *pszIP, const unsigned short shPort,
                                bool bReuse = false);
  static int AcceptOneNonBlock(int fd, int &conn_fd);
  // 单次非阻塞 send. 返回:
  //   >  0: 实际写入字节数 (可能 < len)
  //   == 0: EAGAIN/EWOULDBLOCK/EINTR, 应稍后重试
  //   <  0: 致命错误, 应关闭连接
  static int Send(int fd, const char *buf, int len);
};


} // namespace Network