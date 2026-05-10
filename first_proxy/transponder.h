#pragma once

#include <assert.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <errno.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <signal.h>
#include <string.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "channel.h"
#include "network.h"

namespace Transponder {

struct Endpoint {
  int peer_fd = -1;
  // 待发字节是 tx_buf[tx_off .. tx_buf.size()). 用偏移量避免 erase(0, k)
  // 带来的 O(n^2) 内存搬运.
  std::string tx_buf;
  size_t tx_off = 0;
  bool out_registered = false;
};

inline size_t PendingBytes(const Endpoint &ep) {
  return ep.tx_buf.size() - ep.tx_off;
}

inline void CompactIfEmpty(Endpoint &ep) {
  if (ep.tx_off == ep.tx_buf.size()) {
    ep.tx_buf.clear();
    ep.tx_off = 0;
  }
}

class Transponder : public Channel::PairSink {
public:
  struct Worker {
    int worker_id = 0;
    std::shared_ptr<std::thread> thread;

    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::unique_ptr<Channel::ChannelPair>> pending;

    std::unordered_map<int, Endpoint> endpoints;
  };

  explicit Transponder(uint32_t thread_num) : thread_num_(thread_num) {}

  ~Transponder() override {
    for (uint32_t i = 0; i < thread_num_; ++i) {
      if (workers_[i] && workers_[i]->thread) {
        workers_[i]->thread->join();
      }
    }
  }

  int Spawn() {
    workers_.resize(thread_num_);
    std::atomic<uint32_t> started_num{0};
    for (uint32_t i = 0; i < thread_num_; ++i) {
      workers_[i] = std::unique_ptr<Worker>(new Worker());
      workers_[i]->worker_id = static_cast<int>(i);
    }
    for (uint32_t i = 0; i < thread_num_; ++i) {
      Worker *w = workers_[i].get();
      workers_[i]->thread = std::make_shared<std::thread>([this, w,
                                                           &started_num]() {
        started_num.fetch_add(1, std::memory_order_relaxed);
        Transpond(*w);
      });
    }
    while (started_num.load(std::memory_order_relaxed) != thread_num_) {
      std::this_thread::yield();
    }
    return 0;
  }

  void Submit(std::unique_ptr<Channel::ChannelPair> pair) override {
    if (thread_num_ == 0 || workers_.empty()) {
      return;
    }
    uint32_t idx =
        next_worker_.fetch_add(1, std::memory_order_relaxed) % thread_num_;
    Worker *w = workers_[idx].get();
    {
      std::lock_guard<std::mutex> lk(w->mu);
      w->pending.push_back(std::move(pair));
    }
    w->cv.notify_one();
  }

private:
  void Transpond(Worker &w) {
    int epoll_fd = Network::NetFunction::CreateEpoll();
    assert(epoll_fd >= 0);

    const int MAX_EVENTS = 2048;
    static thread_local epoll_event events[MAX_EVENTS];
    char buf[4096];

    while (true) {
      DrainPending(w, epoll_fd);

      int timeout_ms = w.endpoints.empty() ? 0 : 50;
      int n = Network::NetFunction::EpollWait(epoll_fd, events, MAX_EVENTS,
                                              timeout_ms);
      if (n < 0) {
        std::cout << "thread " << w.worker_id
                  << " EpollWait error: " << strerror(errno) << std::endl;
        continue;
      }

      for (int i = 0; i < n; ++i) {
        int fd = events[i].data.fd;
        uint32_t evs = events[i].events;

        if (evs & (EPOLLERR | EPOLLHUP)) {
          ClosePair(w, epoll_fd, fd);
          continue;
        }

        if (evs & EPOLLOUT) {
          if (Flush(w, epoll_fd, fd) < 0) {
            ClosePair(w, epoll_fd, fd);
            continue;
          }
          if (w.endpoints.find(fd) == w.endpoints.end()) {
            continue;
          }
        }

        if (evs & EPOLLIN) {
          auto it = w.endpoints.find(fd);
          if (it == w.endpoints.end()) {
            continue;
          }
          int peer = it->second.peer_fd;
          ssize_t r = recv(fd, buf, sizeof(buf), 0);
          if (r > 0) {
            if (EnqueueWrite(w, epoll_fd, peer, buf,
                             static_cast<int>(r)) < 0) {
              ClosePair(w, epoll_fd, fd);
              continue;
            }
          } else if (r == 0) {
            ClosePair(w, epoll_fd, fd);
            continue;
          } else if (errno != EAGAIN && errno != EWOULDBLOCK &&
                     errno != EINTR) {
            ClosePair(w, epoll_fd, fd);
            continue;
          }
        }
      }
    }
  }

  // 把 [data, data+n) 发给 fd; 写不完就追加 tx_buf 并 EnableEpollWrite.
  // 返回 0 成功 (含挂缓冲), -1 致命错误.
  static int EnqueueWrite(Worker &w, int epoll_fd, int fd, const char *data,
                          int n) {
    auto it = w.endpoints.find(fd);
    if (it == w.endpoints.end()) {
      return 0; // peer 已关闭, 数据丢弃
    }
    Endpoint &ep = it->second;

    int offset = 0;
    if (PendingBytes(ep) == 0) {
      int wrote = Network::NetFunction::Send(fd, data, n);
      if (wrote < 0) {
        return -1;
      }
      offset = wrote;
      if (offset == n) {
        return 0;
      }
    }
    ep.tx_buf.append(data + offset, n - offset);
    if (!ep.out_registered) {
      if (Network::NetFunction::EnableEpollWrite(epoll_fd, fd) != 0) {
        return -1;
      }
      ep.out_registered = true;
    }
    return 0;
  }

  // 标准 LT 写法: 每次 EPOLLOUT 只 send 一次, 写多少算多少, 立刻返回.
  // 没写完 -> 下一轮 epoll_wait 还会报 EPOLLOUT, 再来 Flush.
  // 写完了 -> 立刻 DisableEpollWrite, 维持 "tx_buf 空 <=> 不监听 EPOLLOUT".
  // 返回 0 成功, -1 致命错误.
  static int Flush(Worker &w, int epoll_fd, int fd) {
    auto it = w.endpoints.find(fd);
    if (it == w.endpoints.end()) {
      return 0;
    }
    Endpoint &ep = it->second;

    if (PendingBytes(ep) > 0) {
      int wrote = Network::NetFunction::Send(
          fd, ep.tx_buf.data() + ep.tx_off,
          static_cast<int>(PendingBytes(ep)));
      if (wrote < 0) {
        return -1;
      }
      if (wrote == 0) {
        return 0; // EAGAIN, 等下次 EPOLLOUT
      }
      ep.tx_off += static_cast<size_t>(wrote);
      CompactIfEmpty(ep);
    }

    if (PendingBytes(ep) == 0 && ep.out_registered) {
      if (Network::NetFunction::DisableEpollWrite(epoll_fd, fd) != 0) {
        return -1;
      }
      ep.out_registered = false;
    }
    return 0;
  }

  static void ClosePair(Worker &w, int epoll_fd, int any_fd) {
    auto it = w.endpoints.find(any_fd);
    if (it == w.endpoints.end()) {
      return;
    }
    int peer = it->second.peer_fd;
    Network::NetFunction::DelEpoll(epoll_fd, any_fd);
    if (peer >= 0) {
      Network::NetFunction::DelEpoll(epoll_fd, peer);
    }
    w.endpoints.erase(any_fd);
    if (peer >= 0) {
      w.endpoints.erase(peer);
    }
    ::close(any_fd);
    if (peer >= 0) {
      ::close(peer);
    }
  }

  static void DrainPending(Worker &w, int epoll_fd) {
    std::deque<std::unique_ptr<Channel::ChannelPair>> local;
    {
      std::unique_lock<std::mutex> lk(w.mu);
      if (w.pending.empty() && w.endpoints.empty()) {
        w.cv.wait(lk, [&] { return !w.pending.empty(); });
      }
      local.swap(w.pending);
    }
    for (auto &p : local) {
      int outer_fd = p->outer_fd;
      int inner_fd = p->inner_fd;
      if (Network::NetFunction::SetNonBlock(outer_fd) != 0 ||
          Network::NetFunction::SetNonBlock(inner_fd) != 0) {
        std::cout << "thread " << w.worker_id
                  << " SetNonBlock failed: " << strerror(errno) << std::endl;
        ::close(outer_fd);
        ::close(inner_fd);
        continue;
      }
      if (Network::NetFunction::AddEpoll(epoll_fd, outer_fd) != 0) {
        std::cout << "thread " << w.worker_id
                  << " AddEpoll(outer) failed: " << strerror(errno)
                  << std::endl;
        ::close(outer_fd);
        ::close(inner_fd);
        continue;
      }
      if (Network::NetFunction::AddEpoll(epoll_fd, inner_fd) != 0) {
        std::cout << "thread " << w.worker_id
                  << " AddEpoll(inner) failed: " << strerror(errno)
                  << std::endl;
        Network::NetFunction::DelEpoll(epoll_fd, outer_fd);
        ::close(outer_fd);
        ::close(inner_fd);
        continue;
      }
      Endpoint outer_ep;
      outer_ep.peer_fd = inner_fd;
      Endpoint inner_ep;
      inner_ep.peer_fd = outer_fd;
      w.endpoints.emplace(outer_fd, std::move(outer_ep));
      w.endpoints.emplace(inner_fd, std::move(inner_ep));
      std::cout << "thread " << w.worker_id << " accept pair outer=" << outer_fd
                << " inner=" << inner_fd << std::endl;
    }
  }

private:
  uint32_t thread_num_ = 0;
  std::vector<std::unique_ptr<Worker>> workers_;
  std::atomic<uint32_t> next_worker_{0};
};

} // namespace Transponder
