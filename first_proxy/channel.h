#pragma once

#include <cassert>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "network.h"

namespace Channel {

struct ChannelPair {
  int outer_fd;
  int inner_fd;
  std::vector<std::string> outer_buffer;
  std::vector<std::string> inner_buffer;
};

class PairSink {
public:
  virtual ~PairSink() = default;
  virtual void Submit(std::unique_ptr<ChannelPair> pair) = 0;
};

class ChannelManager {
public:
  static ChannelManager &GetInstance() {
    static ChannelManager manager;
    return manager;
  }

  ChannelManager() = default;

  void Init(int outer_listen_fd, int inner_link_listen_fd,
            int inner_control_fd, PairSink *sink) {
    outer_listen_fd_ = outer_listen_fd;
    inner_control_fd_ = inner_control_fd;
    inner_link_listen_fd_ = inner_link_listen_fd;
    sink_ = sink;

    inner_control_thread_ =
        std::make_shared<std::thread>(&ChannelManager::InnerControlLoop, this);
    inner_link_thread_ =
        std::make_shared<std::thread>(&ChannelManager::InnerLinkLoop, this);
    outer_accept_thread_ =
        std::make_shared<std::thread>(&ChannelManager::OuterAcceptLoop, this);
    dispatch_thread_ =
        std::make_shared<std::thread>(&ChannelManager::DispatchLoop, this);
  }

  ~ChannelManager() {
    if (outer_accept_thread_) outer_accept_thread_->join();
    if (inner_control_thread_) inner_control_thread_->join();
    if (inner_link_thread_) inner_link_thread_->join();
    if (dispatch_thread_) dispatch_thread_->join();
  }

private:
  void OuterAcceptLoop() {
    int epoll_fd = Network::NetFunction::CreateEpoll();
    assert(epoll_fd >= 0);
    assert(Network::NetFunction::AddEpoll(epoll_fd, outer_listen_fd_) == 0);

    const int MAX_EVENTS = 2048;
    std::vector<int> ready_fds;
    ready_fds.reserve(MAX_EVENTS);
    static epoll_event events[MAX_EVENTS];
    while (true) {
      int num_event =
          Network::NetFunction::EpollWait(epoll_fd, events, MAX_EVENTS, -1);
      assert(num_event >= 0);

      sockaddr_in addr;
      for (int i = 0; i < num_event; ++i) {
        auto &ev = events[i];
        auto fd = ev.data.fd;
        if (ev.events & EPOLLIN) {
          int outer_fd;
          if ((outer_fd = Network::NetFunction::Accept(fd, &addr)) < 0) {
            continue;
          }
          ready_fds.push_back(outer_fd);
        }
      }

      if (!ready_fds.empty()) {
        {
          std::lock_guard<std::mutex> lock(mutex_);
          for (int fd : ready_fds) {
            outer_link_queue_.push(fd);
          }
        }
        cv_.notify_one();
        ready_fds.clear();
      }
    }
  }

  void InnerControlLoop() {
    // 乞讨线程, 乞讨second proxy多给点链接
    // 那边应该自适应才对, 得看申请链接的表现. 这里的策略可以灵活一点
  }

  void InnerLinkLoop() {
    int epoll_fd = Network::NetFunction::CreateEpoll();
    assert(epoll_fd >= 0);
    assert(Network::NetFunction::AddEpoll(epoll_fd, inner_link_listen_fd_) ==
           0);

    const int MAX_EVENTS = 2048;
    std::vector<int> ready_fds;
    ready_fds.reserve(MAX_EVENTS);
    static epoll_event events[MAX_EVENTS];
    while (true) {
      int num_event =
          Network::NetFunction::EpollWait(epoll_fd, events, MAX_EVENTS, -1);
      assert(num_event >= 0);

      sockaddr_in addr;
      for (int i = 0; i < num_event; ++i) {
        auto &ev = events[i];
        auto fd = ev.data.fd;
        if (ev.events & EPOLLIN) {
          int inner_fd;
          if ((inner_fd = Network::NetFunction::Accept(fd, &addr)) < 0) {
            continue;
          }
          ready_fds.push_back(inner_fd);
        }
      }

      if (!ready_fds.empty()) {
        {
          std::lock_guard<std::mutex> lock(mutex_);
          for (int fd : ready_fds) {
            inner_link_queue_.push(fd);
          }
        }
        cv_.notify_one();
        ready_fds.clear();
      }
    }
  }

  void DispatchLoop() {
    while (true) {
      int outer_fd = -1;
      int inner_fd = -1;
      {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait(lk, [&] {
          return !outer_link_queue_.empty() && !inner_link_queue_.empty();
        });
        outer_fd = outer_link_queue_.front();
        outer_link_queue_.pop();
        inner_fd = inner_link_queue_.front();
        inner_link_queue_.pop();
      }
      if (sink_) {
        auto pair = std::unique_ptr<ChannelPair>(
            new ChannelPair{outer_fd, inner_fd, {}, {}});
        sink_->Submit(std::move(pair));
      }
    }
  }

private:
  int outer_listen_fd_ = -1;
  int inner_control_fd_ = -1;
  int inner_link_listen_fd_ = -1;
  PairSink *sink_ = nullptr;

  std::shared_ptr<std::thread> outer_accept_thread_;
  std::shared_ptr<std::thread> inner_control_thread_;
  std::shared_ptr<std::thread> inner_link_thread_;
  std::shared_ptr<std::thread> dispatch_thread_;

  std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<int> outer_link_queue_;
  std::queue<int> inner_link_queue_;
};

} // namespace Channel
