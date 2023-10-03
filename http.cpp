#include <unistd.h>
#include <functional>
#include <iostream>

#include "io.h"
#include "http.h"

namespace HTTP {


// getline by fd
inline bool getline(int fd, char *buf, int& len) {
  len = 0;
  for (size_t i = 0;; i++) {
    char byte;
    auto n = read(fd, &byte, 1);

    if (n < 0) {
      return false;
    } else if (n == 0) {
      if (i == 0) {
        return false;
      } else {
        break;
      }
    }

    buf[len++] = byte;
    if (len >= 2000) {
      buf[len] = '\0';
      return false;
    }

    if (byte == '\n') { break; }
  }
  buf[len] = '\0';
  return true;
}

static inline bool is_space_or_tab(char c) { return c == ' ' || c == '\t'; }

inline bool parse_header(const char *beg, const char *end, std::function<void(std::string &&, std::string &&)> fn) {
  // Skip trailing spaces and tabs.
  while (beg < end && is_space_or_tab(end[-1])) {
    end--;
  }

  auto p = beg;
  while (p < end && *p != ':') {
    p++;
  }

  if (p == end) { return false; }

  auto key_end = p;

  if (*p++ != ':') { return false; }

  while (p < end && is_space_or_tab(*p)) {
    p++;
  }

  if (p < end) {
    auto key = std::string(beg, key_end);
    auto val = std::string(p, end);
    fn(std::move(key), std::move(val));
    return true;
  }

  return false;
}

// judge the request type: CONNECT, GET, POST
int check_req_type() { // 读一个字符
  // TODO
  return 0;
}

int read_http_req(int sockfd, Request& req) {
  char buf[2048];
  int len = 0;
  while (1) {
    if (!getline(sockfd, buf, len)) return -1;

    // Check if the line ends with CRLF.
    auto line_terminator_len = 2;
    if (len >= 2 && buf[len - 2] == '\r' && buf[len - 1] == '\n') {
      // Blank line indicates end of headers.
      if (len == 2) { break; }
    } else {
      continue; // Skip invalid line.
    }

    // Exclude line terminator
    auto end = buf + len - line_terminator_len;

    if (req.method.empty()) {
      auto p = buf;
      if (*p == 'C') {
        req.method = "CONNECT";
      } else if (*p == 'G') {
        req.method = "GET";
      } else if (*p == 'P') {
        req.method = "POST";
      } else {
        return -1;
      }
      continue;
    }

    if (req.method == "GET" || req.method == "POST") {
      printf("line: %s\n", buf);
    }

    // 等待解析key value
    bool parse_ok = parse_header(buf, end, [&](std::string &&key, std::string &&val) {
      // std::cout << "key: " << key << " val: " << val << std::endl;
      if (key == "Host") {
        req.target = val;
      }
    });
    if (!parse_ok) {
      return -1;
    }
  }
  // std::cout << req.to_string() << std::endl;
  return 0;
}

int HttpHandler::handle() {
  int ret = HttpHandler::decode_req_method(fd_, req_);
  if (ret != 0) {
      return -1;
  }
  std::cout << "decode method = " << req_.method << std::endl;
  
  auto &method = req_.method;
  if (method == "CONNECT") {
      return handle_connect();
  } else if (method == "GET") {
      return handle_get();
  } else if (method == "POST") {
      return handle_post();
  } else {
      std::cout << "error method: " << method << std::endl;
      return -1;
  }
  return 0;
}

int HttpHandler::handle_connect() {
  int ret = [&](){
    char buf[2048];
    int len = 0;
    while (1) {
      if (!getline(fd_, buf, len)) return -1;

      // Check if the line ends with CRLF.
      auto line_terminator_len = 2;
      if (len >= 2 && buf[len - 2] == '\r' && buf[len - 1] == '\n') {
        // Blank line indicates end of headers.
        if (len == 2) { break; }
      } else {
        continue; // Skip invalid line.
      }

      // Exclude line terminator
      auto end = buf + len - line_terminator_len;

      // 等待解析key value
      bool parse_ok = parse_header(buf, end, [&](std::string &&key, std::string &&val) {
        if (key == "Host") {
          req_.target = val;
        }
      });
      if (!parse_ok) {
        return -1;
      }
    }
    if (!req_.target.empty()) {
      return 0;
    } else {
      return -1;
    }
  }();
  if (ret != 0) {
    return -1;
  }


  auto& target = req_.target;
  auto pos = target.find(':');
  if (pos == std::string::npos) {
    std::cout << "error parse target: " << target << std::endl;
    return -1;
  }

  std::string host(target.substr(0, pos));
  std::string port(target.substr(pos + 1));
  target_fd_ = IO::CreateConn(host, std::stoi(port));
  if (target_fd_ < 0) {
    std::cout << "CreateConn error: " << ret << std::endl;
    return -1;
  }

  // return http 200
  static const std::string http_200 = "HTTP/1.1 200 Connection Established\r\n\r\n";
  auto len = IO::SendUntilAll(fd_, http_200.c_str(), http_200.size());
  if (len < 0) {
    close(target_fd_);
    return -1;
  }

  std::cout << "connect to " << host << ":" << port << " success" << std::endl;

  return 0;
}

int HttpHandler::handle_get() {
  // TODO
  return -1;
}

int HttpHandler::handle_post() {
  // TODO
  return -1;
}

} // namespace HTTP