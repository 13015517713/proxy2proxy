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

} // namespace HTTP