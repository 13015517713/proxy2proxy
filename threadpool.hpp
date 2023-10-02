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

#include "http.h"
#include "io.h"

namespace ThreadPool {

  class ThreadPool {
    public:
    struct Worker {
      int id_;
      std::thread* thread_;
      // 等待链接的socket
      int wait_fd_;
      // socket映射
      std::unordered_map<int, int> local2remote_;
      std::unordered_map<int, int> remote2local_;
      
    };

    public:

    ThreadPool(int n): thread_len_(n), workers_{n}, threads_{n} {}

    void Func(Worker& w) {
      int epoll_fd = IO::CreateEpoll();
      if (epoll_fd < 0) {
        std::cout << "CreateEpoll error" << std::endl;
        return;
      }
      
      int ret = 0;
      w.wait_fd_ = -1;
      int target_fd = -1;

      int fd;
      while (1) {
        if (w.local2remote_.empty() && w.wait_fd_ == -1){
          std::unique_lock<std::mutex> lock(mutex_fds_);
          cv_fds_.wait(lock, [&](){ return fds_.size() > 0; });
          fd = take();
          if (fd == -1) {
            continue;
          }
          w.wait_fd_ = fd;
          std::cout << "thread id: " << w.id_ << " fd: " << fd << std::endl;
        }

        if (w.wait_fd_ != -1) {
          bool handle_ok = true;
          // 解析http请求并构建代理
          do {
            HTTP::Request req;
            HTTP::read_http_req(w.wait_fd_, req);
            if (ret != 0) {
              std::cout << "read_http_req error: " << ret << std::endl;
              handle_ok = false;
              break;
            }
            std::cout << req.to_string() << std::endl;

            auto& mth = req.method;
            auto& target = req.target;
            auto pos = target.find(':');
            if (pos == std::string::npos) {
              std::cout << "error target: " << target << std::endl;
              handle_ok = false;
              break;
            }

            std::string host(target.substr(0, pos));
            std::string port(target.substr(pos + 1));
            std::cout << "host: " << host << " port: " << port << std::endl;
            target_fd = IO::CreateConn(host, std::stoi(port));
            if (ret < 0) {
              std::cout << "CreateConn error: " << ret << std::endl;
              handle_ok = false;
              break;
            } else {
              std::cout << "CreateConn success: " << target_fd << std::endl;
            }

            // return http 200
            static const std::string http_200 = "HTTP/1.1 200 Connection Established\r\n\r\n";
            auto len = IO::SendUntilAll(w.wait_fd_, http_200.c_str(), http_200.size());
            if (len < 0) {
              std::cout << "SendUntilAll error: " << len << std::endl;
              handle_ok = false;
              break;
            }

            w.local2remote_[w.wait_fd_] = target_fd;
            w.remote2local_[target_fd] = w.wait_fd_;
          }while(0);

          if (!handle_ok) {
            close(w.wait_fd_);
          } else {
            // add epoll
            if (IO::AddEpoll(epoll_fd, w.wait_fd_) == 0) {
              if (IO::AddEpoll(epoll_fd, target_fd) == 0) {
                goto add_epoll_ok;
              } else {
                IO::DelEpoll(epoll_fd, w.wait_fd_);
              }
            }
            std::cout << "AddEpoll error" << std::endl;
            close(w.wait_fd_);
            close(target_fd);
            w.local2remote_.erase(w.wait_fd_);
            w.remote2local_.erase(target_fd);

            add_epoll_ok:;
          }

          w.wait_fd_ = -1;
        }

        // epoll wait
        const int MAX_EVENTS = 10;
        epoll_event events[MAX_EVENTS];
        auto num_event = IO::EpollWait(epoll_fd, events, MAX_EVENTS, 0);
        if (num_event < 0) {
          std::cout << "EpollWait error" << std::endl;
          continue;
        }

        for (int i=0; i<num_event; ++i) {
          auto& ev = events[i];
          auto fd = ev.data.fd;
          if (ev.events & EPOLLIN) {
            char buf[256];
            int len = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
            if (len > 0) {
              std::cout << "read " << len << " bytes from " << fd << std::endl;
              auto target_fd = w.local2remote_.count(fd) ? w.local2remote_[fd] : w.remote2local_[fd];
              int ret = IO::SendUntilAll(target_fd, buf, len);
              if (ret < 0) {
                // 2.对端出错，关闭接收
                shutdown(fd, SHUT_RD);
                // 可以删除了
                IO::DelEpoll(epoll_fd, fd);
              }
            } else if (len < 0) {
              if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
              } else {
                // 这个和下面都正常关闭和删除

                // 1.接收端出错
                std::cout << "read error: " << strerror(errno) << std::endl;
                shutdown(target_fd, SHUT_WR);
              }
            } else {
              // len = 0
              close(fd);
              shutdown(target_fd, SHUT_WR);
              if (IO::DelEpoll(epoll_fd, fd) == 0) {
                if (w.local2remote_.count(fd)) {
                  w.local2remote_.erase(fd);
                } else {
                  w.remote2local_.erase(fd);
                }
              }
            }

          }
        }
        
        w.wait_fd_ = take(); // 拿不拿都行
      }
      
    }

    // spawn, 初始化N个线程
    int spawn() {
      int32_t n = 0;
      for (int i = 0; i < thread_len_; ++i) {
        workers_[i].id_ = i;
        threads_[i] = std::thread([&, &w=workers_[i]] () {
          w.thread_ = &threads_[w.id_];
          {
            std::unique_lock<std::mutex> lock(mutex_);
            if (++n == thread_len_) {
              cv_.notify_one();
            }
          }

          Func(workers_[i]);

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
      std::lock_guard<std::mutex> lock(mutex_);
      if (fds_.size() == 0) {
        return -1;
      }
      int fd = fds_.front();
      fds_.pop_front();
      return fd;
    }

    void submit(int connfd) {
      {
        std::lock_guard<std::mutex> lock(mutex_fds_);
        fds_.push_back(connfd);
      }
      cv_fds_.notify_one();
    }

    ~ThreadPool () {
      for (int i = 0; i < thread_len_; ++i) {
        threads_[i].join();
      }
    }

    private:
    
    int thread_len_;
    std::vector<Worker> workers_;
    std::vector<std::thread> threads_;
    
    // for create thread
    std::mutex mutex_;
    std::condition_variable cv_;
    
    std::deque<int> fds_;
    std::mutex mutex_fds_;
    std::condition_variable cv_fds_;
  };
}