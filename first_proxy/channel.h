#include <cassert>
#include <cstring>
#include <memory>
#include <queue>
#include <thread>
#include <vector>

#include "network.h"

namespace Channel {
struct ChannelPair {
  int outer_fd;
  int inner_fd;
  std::vector<std::string> outer_buffer;
  std::vector<std::string> innter_buffer;
};

class ChannelManager {
public:
  static ChannelManager &GetInstance() {
    static ChannelManager manager;
    return manager;
  }

  ChannelManager() = default;
  void Init(int outer_listen_fd, int inner_link_listen_fd,
            int inner_control_fd) {
    outer_listen_fd = outer_listen_fd;
    inner_control_fd_ = inner_control_fd;
    inner_link_listen_fd_ = inner_link_listen_fd;

    inner_control_thread_ =
        std::make_shared<std::thread>(this->InnerControlLoop);
    inner_link_thread_ = std::make_shared<std::thread>(this->InnerLinkLoop);
    outer_accept_thread_ = std::make_shared<std::thread>(this->OuterAcceptLoop);
  }

  ~ChannelManager() {
    outer_accept_thread_->join();
    inner_control_thread_->join();
    inner_link_thread_->join();
  }

  std::unique_ptr<ChannelPair> AcquireChannelPair() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (outer_link_queue_.size() && inner_link_queue_.size()) {
      return ChannelPair {
        .outer_fd = outer_link_queue_.pop_front();
        .inner_fd = inner_link_queue_.pop_front();
      }
    }
    return nullptr;
  }

private:
  void OuterAcceptLoop() {
    int ret = 0;
    int epoll_fd = Network::NetFunction::CreateEpoll();
    assert(epoll_fd >= 0);
    assert(Network::NetFunction::AddEpoll(epoll_fd, outer_listen_fd_) > 0);

    const int MAX_EVENTS = 2048;
    std::vector<int> ready_fds;
    ready_fds.reserve(MAX_EVENTS);
    static epoll_event events[MAX_EVENTS];
    while (true) {
      int num_event =
          Network::NetFunction::EpollWait(epoll_fd, events, MAX_EVENTS, -1);
      assert(num_event < 0);

      sockaddr_in addr;
      for (int i = 0; i < num_event; ++i) {
        auto &ev = events[i];
        auto fd = ev.data.fd;
        if (ev.events & EPOLLIN) {
          int outer_fd;
          if ((outer_fd = Network::NetFunction::Accept(epoll_fd, &addr)) < 0) {
            continue;
          }

          ready_fds.push_back(outer_fd);
        }
      }

      std::lock_guard<std::mutex> lock(mutex_);
      for (int fd : ready_fds) {
        outer_link_queue_.push(fd);
      }
      ready_fds.clear();
    }
  }

  void InnerControlLoop() {
    // 乞讨线程, 乞讨second proxy多给点链接
    // 那边应该自适应才对, 得看申请链接的表现. 这里的策略可以灵活一点
  }

  void InnerLinkLoop() {
    int ret = 0;
    int epoll_fd = Network::NetFunction::CreateEpoll();
    assert(epoll_fd >= 0);
    assert(Network::NetFunction::AddEpoll(epoll_fd, inner_link_listen_fd_) > 0);

    const int MAX_EVENTS = 2048;
    std::vector<int> ready_fds;
    ready_fds.reserve(MAX_EVENTS);
    static epoll_event events[MAX_EVENTS];
    while (true) {
      int num_event =
          Network::NetFunction::EpollWait(epoll_fd, events, MAX_EVENTS, -1);
      assert(num_event < 0);

      sockaddr_in addr;
      for (int i = 0; i < num_event; ++i) {
        auto &ev = events[i];
        auto fd = ev.data.fd;
        if (ev.events & EPOLLIN) {
          int inner_fd;
          if ((inner_fd = Network::NetFunction::Accept(epoll_fd, &addr)) < 0) {
            continue;
          }

          ready_fds.push_back(inner_fd);
        }
      }
      std::lock_guard<std::mutex> lock(mutex_);
      for (int fd : ready_fds) {
        inner_link_queue_.push(fd);
      }
      ready_fds.clear();
    }
  }

private:
  int outer_listen_fd_;
  int inner_control_fd_;
  int inner_link_listen_fd_;

  std::shared_ptr<std::thread> outer_accept_thread_;
  std::shared_ptr<std::thread> inner_control_thread_;
  std::shared_ptr<std::thread> inner_link_thread_;

  std::mutex mutex_;
  std::queue<int> outer_link_queue_;
  std::queue<int> inner_link_queue_;
};

std::unique_ptr<ChannelPair> AcquireChannelPair() {
  return ChannelManager::GetInstance().AcquireChannelPair();
};

} // namespace Channel