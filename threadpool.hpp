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
#include <set>
#include <sys/epoll.h>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>

namespace ThreadPool {
  using http = boost::beast::http;
  using net = boost::asio;

  class ThreadPool {
    public:
    struct Worker {
      int id_;
      std::thread* thread_;
      // 等待链接的socket
      int wait_fd_;
      // 活跃的socket
      std::set<int> active_;
      // 每个socket应该做的映射
    };

    public:

    ThreadPool(int n): thread_len_(n), workers_{n}, threads_{n} {}

    void Func(Worker& w) {
      net::streambuf sb;
      int epoll_fd = epoll_create1(0);
      w.wait_fd_ = -1;

      while (1) {
        if (true){
          std::unique_lock<std::mutex> lock(mutex_fds_);
          cv_fds_.wait(lock, [&](){ return fds_.size() > 0; });
          int fd = take();
          if (fd == -1) {
            continue;
          }
          w.wait_fd_ = fd;
          std::cout << "thread id: " << w.id_ << " fd: " << fd << std::endl;
        }

        if (w.wait_fd_ != -1) {
          http::request<http::string_body> req;
          http::read(w.wait_fd_, sb, req); // 报错处理

          auto mth = std::string(req.method_string());
			    auto target_view = std::string(req.target()); // 怎么解析出来的，直接用boost的http解析
			    auto pa = std::string(req[http::field::proxy_authorization]);
          
          // do connect

          w.active_.insert(w.wait_fd_);
          w.wait_fd_ = -1;
        }

        // 收到的每个链接都正常转发。

        // 处理完去抢fd
        
        // 我不能一直循环啊，没有任务的时候是怎么处理的
        take(); // 再尝试拿一个
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