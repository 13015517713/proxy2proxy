/*
逻辑：N线程，任务过来，放到队列中，唤醒线程去争抢。
*/

#pragma once
#include <vector>
#include <thread>
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <deque>
#include <unordered_map>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <string.h>

#include "http.h"
#include "io.h"

namespace ThreadPool {
  static inline void thread_init() {
    // 忽略SIGPIPE信号
    signal(SIGPIPE, SIG_IGN);
  }

  class ThreadPool {
    public:
    struct Worker {
      int id_;
      std::thread* thread_;
      // 等待链接的socket
      // int wait_fd_;
      std::deque<int> wait_fds_; // feat: 优化，等待链接的socket，可以有多个
      // socket映射
      std::unordered_map<int, int> local2remote_;
      std::unordered_map<int, int> remote2local_;
    };

    public:

    ThreadPool(int n): thread_len_(n), workers_{n}, threads_{n} {}

    int AddSocketLink(Worker &w, int epollfd, int fd) {

      int ret = take_socket_link();
      if (ret < 0) {
        return -2;
      }

      auto target_fd = ret;
      auto add_ok = false;
      
      do {
        ret = IO::AddEpoll(epollfd, fd);
        if (ret != 0) {
          break;
        }
        ret = IO::AddEpoll(epollfd, target_fd);
        if (ret != 0) {
          IO::DelEpoll(epollfd, fd);
          break;
        }
        w.local2remote_[fd] = target_fd;
        w.remote2local_[target_fd] = fd;
        add_ok = true;
      }while(0);
      
      if (!add_ok) {
         close(target_fd);
         return -1;
      }

      return 0;
    }

    void Func(Worker& w) {
      
      thread_init();

      int epoll_fd = IO::CreateEpoll();
      if (epoll_fd < 0) {
        std::cout << "CreateEpoll error" << std::endl;
        return;
      }
      
      int ret = 0;
      int target_fd = -1;

      int fd;
      while (1) { 
        if (w.local2remote_.empty() && w.wait_fds_.empty()){
          std::unique_lock<std::recursive_mutex> lock(mutex_fds_);
          cv_fds_.wait(lock, [&](){ return fds_.size() > 0; });
        }

        if (!w.wait_fds_.empty()) {
          while (1) {
            if (w.wait_fds_.empty()) break;
            fd = w.wait_fds_.front();
            ret = AddSocketLink(w, epoll_fd, fd);
            if (ret == 0) {
              // add 成功
              w.wait_fds_.pop_front();
            } else if (ret == -2){
              break;
            } else if (ret == -1) {
              close(fd);
              w.wait_fds_.pop_front();
            }
          }
        }

        // epoll wait
        const int MAX_EVENTS = 256;
        epoll_event events[MAX_EVENTS];
        auto num_event = IO::EpollWait(epoll_fd, events, MAX_EVENTS, 0);
        if (num_event < 0) {
          std::cout << "EpollWait error" << std::endl;
          continue;
        }

        static thread_local char buf[1024];  // 这样会不会效果提升 ? 
        for (int i=0; i<num_event; ++i) {
          auto& ev = events[i];
          auto fd = ev.data.fd;
          auto target_fd = w.local2remote_.count(fd) ? w.local2remote_[fd] : w.remote2local_[fd];
          if (ev.events & EPOLLIN) {
            int len = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
            // std::cout << "thread " << w.id_ << " recv " << len << " bytes from " << fd << std::endl;
            if (len > 0) {
              int ret = IO::SendUntilAll(target_fd, buf, len);
              // std::cout << "thread " << w.id_ << " send " << ret << " bytes to " << target_fd << std::endl;
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
                  // std::cout << "thread " << w.id_ << " close " << fd << std::endl;
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
                // std::cout << "thread " << w.id_ << " close " << fd << std::endl;
              }
            }

          }
        }
        
        uint16_t push_size = push_some(w);
        ret = IO::SendUntilAll(control_fd_, (char*)&push_size, sizeof(push_size));
        if (ret < 0) {
          exit(-1); // 没法申请了，直接退出
        }
      }

    }

    // spawn, 初始化N个线程
    int spawn() {
      int32_t n = 0;
      for (int i = 0; i < thread_len_; ++i) {
        workers_[i].id_ = i;
        auto &w = workers_[i];
        threads_[i] = std::thread([&] () {
          w.thread_ = &threads_[w.id_];
          {
            std::unique_lock<std::mutex> lock(mutex_);
            if (++n == thread_len_) {
              cv_.notify_one();
            }
          }

          Func(w);

        });
      }

      // wait all ok
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&](){ return n == thread_len_; });
      }
      std::cout << "thread init success." << std::endl;
      return 0;
    }

    // 从队列中取出任务
    int take() {
      std::lock_guard<std::recursive_mutex> lock(mutex_fds_);
      if (fds_.size() == 0) {
        return -1;
      }
      int fd = fds_.front();
      fds_.pop_front();
      return fd;
    }

    // 拿出部分
    uint16_t push_some(Worker& w) {
      std::lock_guard<std::recursive_mutex> lock(mutex_fds_);
      auto push_size = std::max(int(fds_.size() / thread_len_), 10);
      push_size = std::min(push_size, (int)fds_.size());
      for (int i = 0; i < push_size; ++i) {
        w.wait_fds_.push_back(fds_.front());
        fds_.pop_front();
      }
      return static_cast<uint16_t>(push_size);
    }

    void submit(int fd) {
      {
        std::lock_guard<std::recursive_mutex> lock(mutex_fds_);
        fds_.push_back(fd);
      }
      cv_fds_.notify_one();
    }

    int take_socket_link() {
      std::lock_guard<std::mutex> lock(mutex_link_fds_);
      if (link_fds_.size() == 0) {
        return -1;
      }
      int fd = link_fds_.front();
      link_fds_.pop_front();
      return fd;
    }

    void submit_socket_link(int fd) {
      std::lock_guard<std::mutex> lock(mutex_link_fds_);
      link_fds_.push_back(fd);
    }

    ~ThreadPool () {
      for (int i = 0; i < thread_len_; ++i) {
        threads_[i].join();
      }
    }

    int control_fd_;

    private:
    
    int thread_len_;
    std::vector<Worker> workers_;
    std::vector<std::thread> threads_;
    
    // for create thread
    std::mutex mutex_;
    std::condition_variable cv_;
    
    std::deque<int> fds_;
    std::recursive_mutex mutex_fds_;
    std::condition_variable_any cv_fds_;

    std::deque<int> link_fds_;
    std::mutex mutex_link_fds_;
  };
}