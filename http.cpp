#include <unistd.h>
#include <functional>
#include <iostream>
#include <cstring>

#include "io.h"
#include "http.h"

namespace HTTP {


// getline by fd
inline bool getline(int fd, char *buf, int& len) {
  len = 0;
  for (size_t i = 0;; i++) {
    char byte;
    auto n = 0;
    n = read(fd, &byte, 1);

    if (n < 0) {
      std::cout << "read error: " << strerror(errno) << std::endl;
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

struct MethodLine{
  std::string method;
  std::string url;
  std::string version;
};

static int DecodeMethonLine(const char *buf, int len, MethodLine& method_line) {
  auto p = buf;
  auto end = buf + len;
  while (p < end && *p != ' ') p++;
  if (p == end) return -1;
  method_line.method = std::string(buf, p);

  while (p < end && *p == ' ') p++;
  if (p == end) return -1;
  auto url_beg = p;
  while (p < end && *p != ' ') p++;
  if (p == end) return -1;
  auto url_end = p;
  method_line.url = std::string(url_beg, url_end);

  while (p < end && *p == ' ') p++;
  if (p == end) return -1;
  auto version_beg = p;
  while (p < end && *p != ' ') p++;
  // if (p == end) return -1;
  auto version_end = p;
  method_line.version = std::string(version_beg, version_end);

  return 0;
}

int HttpHandler::handle_connect() {
  MethodLine method_line;
  int ret = [&](){
    char buf[2048];
    int len = 0;
    while (1) {
      if (!getline(fd_, buf, len)) return -1;
      // if (send_buf.empty()) { // 暂时不需要
      //   int ret = DecodeMethonLine(buf, len, method_line);
      //   if (ret != 0) {
      //     return -1;
      //   }
      // }

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

static inline int decode_http_or_https(const std::string &url) {
  if (url.size() < 8) {
    return -1;
  }
  if (url.substr(0, 7) == "http://") {
    return 0;
  } else if (url.substr(0, 8) == "https://") {
    return 1;
  } else {
    return -1;
  }
}

int HttpHandler::handle_get() {
  std::string send_buf;
  MethodLine method_line;
  send_buf.reserve(2048);
  uint16_t default_port = 0;

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

      if (send_buf.empty()) {
        int ret = DecodeMethonLine(buf, len-line_terminator_len, method_line);
        if (ret != 0) {
          return -1;
        }
        ret = decode_http_or_https(method_line.url);
        if (ret < 0) {
          return -1;
        }
        if (ret == 1) {
          default_port = 443;
        } else {
          default_port = 80;
        }
      }
      send_buf.append(buf, len);

      // Exclude line terminator
      auto end = buf + len - line_terminator_len;

      // 等待解析key value
      bool parse_ok = parse_header(buf, end, [&](std::string &&key, std::string &&val) {
        if (key == "Host") {
          req_.target = val;
        } else if (key == "Upgrade-Insecure-Requests") {
          if (val == "1") {
            default_port = 443;
          }
        }
      });
      if (!parse_ok) {
        return -1;
      }
    }
    return 0;
  }();
  if (ret != 0) {
    return -1;
  }
  
  std::cout << "send buf: " << send_buf.c_str() << std::endl;
  auto& target = req_.target;
  auto pos = target.find(':');
  std::string host;
  uint16_t port;
  
  if (pos == std::string::npos) {
    host = target;
    port = default_port;
  } else {
    host = target.substr(0, pos);
    port = std::stoi(target.substr(pos + 1));
  }

  target_fd_ = IO::CreateConn(host, port);
  if (target_fd_ < 0) {
    std::cout << "CreateConn error: " << ret << std::endl;
    return -1;
  }

  // send to target
  auto len = IO::SendUntilAll(target_fd_, send_buf.c_str(), send_buf.size());
  if (len < 0) {
    close(target_fd_);
    return -1;
  }


  // // 测试接收
  // ret = [&](){
  //   std::string recv_buf;
  //   char buf[2048];
  //   int len = 0;
  //   while (1) {
  //     if (!getline(target_fd_, buf, len)) return -1;
      
  //     // Check if the line ends with CRLF.
  //     auto line_terminator_len = 2;
  //     if (len >= 2 && buf[len - 2] == '\r' && buf[len - 1] == '\n') {
  //       // Blank line indicates end of headers.
  //       if (len == 2) { break; }
  //     } else {
  //       continue; // Skip invalid line.
  //     }
  //     recv_buf.append(buf, len);

  //     // Exclude line terminator
  //     auto end = buf + len - line_terminator_len;
  //   }
  //   std::cout << "Get: recv buf: " << recv_buf.c_str() << std::endl;
  //   return 0;
  // }();

  // return http 200: 不需要发吧
  // static const std::string http_200 = "HTTP/1.1 200 Connection Established\r\n\r\n";
  // len = IO::SendUntilAll(fd_, http_200.c_str(), http_200.size());
  // if (len < 0) {
  //   close(target_fd_);
  //   return -1;
  // }

  std::cout << "Get: connect to " << host << ":" << port << " success" << std::endl;

  return 0;
}

int HttpHandler::handle_post() {
  return handle_get();
}

} // namespace HTTP