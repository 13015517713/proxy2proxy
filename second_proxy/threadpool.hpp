#include <deque>
#include <thread>
#include <vector>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <condition_variable>
#include <winsock2.h>

#include "io.h"

namespace EventLoop {

    class EventLoop {
        public:
        struct Worker {
            int id_;
            std::thread* thread_;
            // socket映射
            std::unordered_map<int, int> local2remote_;
            std::unordered_map<int, int> remote2local_;
        };

    public:
        EventLoop(const int thread_len): thread_len_(thread_len), threads_{thread_len}, workers_(thread_len) {}
        
        void Func(Worker& w) {
            // 抢到socket link
            int max_fd = 0;
            fd_set readfds;
            FD_ZERO(&readfds);
            int local_fd, remote_fd;
            int ret = 0;

            while (1) {
                std::cout << "local2remote size: " << w.local2remote_.size() << std::endl;
                if (w.local2remote_.empty()) {
                    std::unique_lock<std::recursive_mutex> lock(mutex_socket_links_);
                    cv_socket_links_.wait(lock, [&](){ return usable_socket_links_.size() > 0; });
                    ret = take(local_fd, remote_fd);
                    if (ret < 0) {
                        continue;
                    }
                    w.local2remote_[local_fd] = remote_fd;
                    w.remote2local_[remote_fd] = local_fd;
                    FD_SET(local_fd, &readfds);
                    FD_SET(remote_fd, &readfds);
                    max_fd = std::max(local_fd, remote_fd);
                }
                
                int ret = IO::Select(readfds, max_fd, {0,0});
                if (ret < 0) {
                    std::cout << "Failed to select" << std::endl;
                    continue;
                }

                // 读写
                for (int fd = 0; fd <= max_fd; ++fd) {
                    if (FD_ISSET(fd, &readfds)) {
                        char buf[256];
                        ret = recv(fd, buf, sizeof(buf), 0);
                        auto target_fd = w.local2remote_.count(fd) ? w.local2remote_[fd] : w.remote2local_[fd];
                        if (ret > 0) {
                            ret = IO::SendUntilAall(target_fd, buf, ret);
                            if (ret < 0) {
                                shutdown(fd, SD_RECEIVE);
                            }
                        } else if (ret <= 0) { 
                            shutdown(target_fd, SD_SEND);
                            FD_CLR(fd, &readfds);
                            if (w.local2remote_.count(fd)) {
                                w.local2remote_.erase(fd);
                            } else {
                                w.remote2local_.erase(fd);
                            }
                        }
                    }
                }
                
                ret = take(local_fd, remote_fd);
                if (ret < 0) {
                    continue;
                }
                w.local2remote_[local_fd] = remote_fd;
                w.remote2local_[remote_fd] = local_fd;
            }

        }

        // spawn threads
        int spawn() {
            int32_t n = 0;
            for (int i = 0; i < thread_len_; ++i) {
                workers_[i].id_ = i;
                auto &w = workers_[i]; // 为了在类外被捕获
                threads_[i] = std::thread([&] () {
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

        int take(int& local_fd, int& remote_fd) {
            std::lock_guard<std::recursive_mutex> lock(mutex_socket_links_);
            if (usable_socket_links_.size() == 0) {
                return -1;
            }
            std::pair<int, int> socket_link = usable_socket_links_.front();
            usable_socket_links_.pop_front();
            local_fd, remote_fd = socket_link.first, socket_link.second;
            return 0;
        }

        void AddSocketLink(int local_fd, int remote_fd) {
            std::cout << "add socket link" << std::endl;
            {
                std::lock_guard<std::recursive_mutex> lock(mutex_socket_links_);
                usable_socket_links_.push_back(std::make_pair(local_fd, remote_fd));
            }
            cv_socket_links_.notify_one();
        }

    private:
        int thread_len_;
        std::vector<Worker> workers_;
        std::vector<std::thread> threads_;

        // construct all threads
        std::mutex mutex_;
        std::condition_variable cv_;
        
        std::recursive_mutex mutex_socket_links_;
        std::condition_variable_any cv_socket_links_;
        std::deque<std::pair<int, int>> usable_socket_links_;
    };

}; // namespace EventLoop