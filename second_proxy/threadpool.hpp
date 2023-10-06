#include <deque>
#include <thread>
#include <vector>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <condition_variable>
#include <winsock2.h>
#include <unistd.h>

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
            // wepoll create fd
            HANDLE epoll_fd = IO::EpollCreate();
            if (epoll_fd == nullptr) {
                std::cout << w.id_ << " Failed to create epoll fd" << std::endl;
                return;
            }

            int local_fd, remote_fd;
            int ret = 0;

            while (1) {
                if (w.local2remote_.empty()) {
                    {
                        std::unique_lock<std::recursive_mutex> lock(mutex_socket_links_);
                        cv_socket_links_.wait(lock, [&](){ return usable_socket_links_.size() > 0; });
                        ret = take(local_fd, remote_fd);
                    }

                    if (ret < 0) {
                        continue;
                    }
                    
                    if (IO::SetNonBlocking(local_fd) < 0 || IO::SetNonBlocking(remote_fd) < 0) {
                        close(local_fd);
                        close(remote_fd);
                        std::cout << "Failed to set nonblocking" << std::endl;
                        continue;
                    }
                    
                    auto add_ok = false;
                    if (IO::AddEpoll(epoll_fd, local_fd) == 0) {
                        if (IO::AddEpoll(epoll_fd, remote_fd) == 0) {
                            w.local2remote_[local_fd] = remote_fd;
                            w.remote2local_[remote_fd] = local_fd;
                            add_ok = true;
                        } else {
                            IO::DelEpoll(epoll_fd, local_fd);
                        }
                    }
                    if (!add_ok) {
                        close(local_fd);
                        close(remote_fd);
                        std::cout << "Failed to add epoll" << std::endl;
                        continue;
                    }
                    std::cout << "thread " << w.id_ << " get socket link " << local_fd << " " << remote_fd << std::endl;
                }
                
                // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                const int max_events = 10;
                epoll_event events[max_events];
                auto num_events = IO::EpollWait(epoll_fd, events, max_events, 0);
                if (num_events < 0) {
                    std::cout << "Epoll wait error." << std::endl;
                    continue;
                }

                for (int i=0; i<num_events; ++i) {
                    auto& ev = events[i];
                    auto fd = ev.data.fd;
                    auto target_fd = w.local2remote_.count(fd) ? w.local2remote_[fd] : w.remote2local_[fd];
                    if (ev.events & EPOLLIN) {
                        char buf[1024];
                        int len = recv(fd, buf, sizeof(buf), 0); // 非阻塞
                        // std::cout << "thread " << w.id_ << " recv " << len << " bytes from " << fd << std::endl;
                        if (len > 0) {
                            int ret = IO::SendUntilAll(target_fd, buf, len);  // 如果对端关闭，这里会返回-1，信号SIGPIPE
                            // std::cout << "thread " << w.id_ << " send " << ret << " bytes to " << target_fd << std::endl;
                            if (ret < 0) {
                                shutdown(fd, SD_RECEIVE);
                            }
                        } else if (len < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                                continue;
                            } else {
                                // std::cout << "read error: " << strerror(errno) << std::endl;
                                shutdown(target_fd, SD_SEND);
                                shutdown(fd, SD_RECEIVE);
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
                            shutdown(fd, SD_RECEIVE);
                            shutdown(target_fd, SD_SEND);
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

                ret = take(local_fd, remote_fd);
                if (ret < 0) {
                    continue;
                }

                auto add_ok = false;
                if (IO::AddEpoll(epoll_fd, local_fd) == 0) {
                    if (IO::AddEpoll(epoll_fd, remote_fd) == 0) {
                        w.local2remote_[local_fd] = remote_fd;
                        w.remote2local_[remote_fd] = local_fd;
                        add_ok = true;
                    } else {
                        IO::DelEpoll(epoll_fd, local_fd);
                    }
                }
                if (!add_ok) {
                    close(local_fd);
                    close(remote_fd);
                    std::cout << "Failed to add epoll" << std::endl;
                }
            }

        }

        // spawn threads
        int spawn() {
            int32_t n = 0;
            for (int i = 0; i < thread_len_; ++i) {
                workers_[i].id_ = i;
                auto &w = workers_[i];
                threads_[i] = std::thread([&] () {
                    w.thread_ = &threads_[i];
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
            local_fd = socket_link.first;
            remote_fd = socket_link.second;
            return 0;
        }

        void AddSocketLink(int local_fd, int remote_fd) {
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