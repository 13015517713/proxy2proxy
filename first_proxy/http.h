#pragma once

#include <string>
#include <set>

namespace HTTP {

    struct Request {
        std::string method;
        std::string target;
        std::string body;
        std::set<std::string> want_keys = {"method", "target", "body"};
        std::string to_string() const {
            return "method: " + method + " target: " + target + " body: " + body;
        }
    };

    int read_http_req(int sockfd, Request& req);

    struct HttpHandler {
        HttpHandler(int fd) : fd_(fd) {}
        static int decode_req_method(int fd, Request& req) {
            char ch;
            int ret;
            while(1) {
                ret = recv(fd, &ch, 1, MSG_PEEK);
                if (ret > 0) {
                    break;
                } else {
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    }
                    return -1;
                }
            }
            switch (ch) {
            case 'C':
                req.method = "CONNECT";
                break;
            case 'G':
                req.method = "GET";
                break;
            case 'P':
                req.method = "POST";
                break;
            default:
                std::cout << "error method: " << ch << std::endl;
                return -1;
            }
            return 0;
        }
        
        int handle();

        int handle_connect();

        int handle_get();

        int handle_post();

        int fd_;
        int target_fd_;
        HTTP::Request req_;
    };
} // namespace HTTP