#include <arpa/inet.h>
#include <cassert>
#include <csignal>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "channel.h"
#include "network.h"
#include "transponder.h"

using namespace Network;

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

  Transponder::Transponder ts(ps.transponder_thread_num);
  ts.Spawn();

  Channel::ChannelManager &channels = Channel::ChannelManager::GetInstance();
  channels.Init(outer_listen_fd, inner_link_listen_fd, inner_control_fd, &ts);

  while (true) {
    sleep(1);
  }
  return 0;
}

int main() {
  signal(SIGPIPE, SIG_IGN);
  return RunTransponder();
}
