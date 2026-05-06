#pragma once

#include <assert.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <signal.h>
#include <string.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "io.h"

namespace Transponder {
class Transponder {
public:
  struct Worker {
    int worker_id;
    std::shared_ptr<std::thread> thread;

    std::unordered_map<int, int> local2remote;
    std::unordered_map<int, int> remote2local;
  };

  Transponder(uint32_t thread_num) : thread_num_(thread_num) {}
  ~Transponder() {
    for (int i = 0; i < thread_num_; ++i) {
      workers_[i].thread->join();
    }
  }

  int Spawn() {
    static std::atomic<uint64_t> started_num = 0;
    workers_.resize(thread_num_);
    for (int i = 0; i < thread_num_; ++i) {
      workers_[i].worker_id = i;
      workers_[i].thread = std::make_shared<std::thread>([&]() {
        Transpond(workers_[i]);
        started_num++;
      });
    }

    while (started_num != thread_num_)
      continue;
    return 0;
  }

private:
  void Transpond(Worker &w) {
    int epoll_fd = Network::NetFunction::CreateEpoll();
    assert(epoll_fd < 0);

    // 1. 处理转发, 是两个双向的channel
    // 2. 处理每个事件, 每个接收和发生都放在缓冲区, 没发出去就还放在缓存
    // 3. 单个Loop后去抢占式的消费SockPair
    while (1) {
      const int MAX_EVENTS = 2048;
      epoll_event events[MAX_EVENTS];
      auto num_event = IO::EpollWait(epoll_fd, events, MAX_EVENTS, 0);
      if (num_event < 0) {
        std::cout << "EpollWait error" << std::endl;
        continue;
      }

      char buf[256];
      for (int i = 0; i < num_event; ++i) {
        auto &ev = events[i];
        auto fd = ev.data.fd;
        auto target_fd = w.local2remote_.count(fd) ? w.local2remote_[fd]
                                                   : w.remote2local_[fd];
        if (ev.events & EPOLLIN) {
          int len = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
          // std::cout << "thread " << w.id_ << " recv " << len << " bytes from
          // " << fd << std::endl;
          if (len > 0) {
            int ret = IO::SendUntilAll(target_fd, buf, len);
            // std::cout << "thread " << w.id_ << " send " << ret << " bytes to
            // " << target_fd << std::endl;
            if (ret < 0) {
              shutdown(fd, SHUT_RD);
            }
          } else if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
              continue;
            } else {
              // std::cout << "read error: " << strerror(errno) << std::endl;
              shutdown(target_fd, SHUT_WR);
              shutdown(fd, SHUT_RD);
              if (IO::DelEpoll(epoll_fd, fd) == 0) {
                if (w.local2remote_.count(fd)) {
                  w.local2remote_.erase(fd);
                } else {
                  w.remote2local_.erase(fd);
                }
                // std::cout << "thread " << w.id_ << " close " << fd <<
                // std::endl;
              }
            }
          } else {
            // len = 0
            shutdown(fd, SHUT_RD);
            shutdown(target_fd, SHUT_WR);
            if (IO::DelEpoll(epoll_fd, fd) == 0) {
              if (w.local2remote_.count(fd)) {
                w.local2remote_.erase(fd);
              } else {
                w.remote2local_.erase(fd);
              }
              // std::cout << "thread " << w.id_ << " close " << fd <<
              // std::endl;
            }
          }
        }
      }

      uint8_t cur_hunger_thread_cnt = hunger_thread_cnt_;
      if (!w.local2remote_.empty() &&
          cur_hunger_thread_cnt > 0) { // 非空且有人饥饿
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      if (w.local2remote_.empty() || cur_hunger_thread_cnt == 0) {
        uint16_t push_size = push_some(w);
        std::string bytes = std::string(push_size, '0');
        ret = IO::SendUntilAll(control_fd_, bytes.c_str(),
                               bytes.size()); // 一个链接一个字节
        if (ret < 0) {
          std::cout << "thread " << w.id_
                    << " send to control error: " << strerror(errno)
                    << std::endl;
          exit(-1); // 没法申请了，直接退出
        }
        if (push_size) {
          std::cout << "thread " << w.id_ << " push " << push_size
                    << " fds to control" << std::endl;
          if (cur_hunger_thread_cnt) {
            hunger_thread_cnt_--;
          }
        }
      }
    }
  }

  int Take() {
    std::lock_guard<std::recursive_mutex> lock(mutex_fds_);
    if (fds_.size() == 0) {
      return -1;
    }
    int fd = fds_.front();
    fds_.pop_front();
    return fd;
  }

private:
  uint64_t thread_num_;
  std::vector<Worker> workers_;
};
} // namespace Transponder