#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>

#include "channel.h"
#include "network.h"
#include "transponder.h"

using namespace Network;

namespace {

int AcceptSocklinkLoop(int fd, ThreadPool::ThreadPool &tp) {
  int cnt = 0;
  struct sockaddr_in addr;
  while (true) {
    int connfd = IO::Accept(fd, NULL);
    if (connfd >= 0) {
      if (IO::SetNonBlock(connfd) != 0) {
        std::cout << "SetNonBlock error" << std::endl;
        continue;
      }
      // 放到全局队列可用队列里
      std::cout << "AcceptSocklink fd: " << connfd << " cnt: " << cnt
                << std::endl;
      tp.submit_socket_link(connfd);
    } else {
      if ((errno == EAGAIN) || (errno == EINTR) || (errno == EWOULDBLOCK)) {
        continue;
      }
      return -1;
    }
  }
  return 0;
}
} // namespace

struct TransponderParams {
  const int transponder_thread_num = 4;
  const int outer_link_port = 12345;
  const int inner_control_port = 12346;
  const int inner_link_port = 12347;
  const char *ip = "0.0.0.0";
};

int RunTransponder() {
  TransponderParams ps;
  int outer_listen_fd =
      NetFunction::CreateListenSocket(ps.ip, ps.outer_link_port, false);
  int inner_link_listen_fd =
      NetFunction::CreateListenSocket(ps.ip, ps.inner_link_port, false);
  int inner_control_listen_fd =
      NetFunction::CreateListenSocket(ps.ip, ps.inner_control_port, false);
  if (outer_listen_fd < 0 || listen(outer_listen_fd, 1024) != 0 ||
      inner_link_listen_fd < 0 || listen(inner_link_listen_fd, 1024) != 0 ||
      inner_control_listen_fd < 0 || listen(inner_control_listen_fd, 1) != 0) {
    return -1;
  }

  // accept inner control from second proxy once
  int inner_control_fd;
  assert(NetFunction::AcceptOneNonBlock(inner_control_listen_fd,
                                        inner_control_fd) == 0);

  Channel::ChannelManager channels = Channel::ChannelManager::GetInstance();
  channels.Init(outer_listen_fd, inner_link_listen_fd, inner_control_fd);

  Transponder::Transponder ts(ps.transponder_thread_num);
  ts.Spawn();

  while (true) {
    sleep(1);
  }
  return 0;
}

int main() {
  signal(SIGPIPE, SIG_IGN);
  return RunTransponder();
}
