#include <deque>
#include <thread>
#include <unordered_map>

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
        EventLoop(const int thread_len): thread_len_(thread_len) {}
        bool AddUsableFd(int fd) {
            usable_fds_.push_back(fd);
        }
    private:
        int thread_len_;
        std::deque<int> usable_fds_;
    }

}; // namespace EventLoop