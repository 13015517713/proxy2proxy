#include <unistd.h>
#include <functional>
#include <iostream>

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

    // printf("line: %s\n", buf);

    if (req.method.empty()) {
      auto p = buf;
      if (*p == 'C') {
        req.method = "CONNECT";
      } else if (*p == 'G') {
        req.method = "GET";
        // TODO
        return -1;
      } else if (*p == 'P') {
        req.method = "POST";
        // TODO
        return -1;
      } else {
        return -1;
      }
      continue;
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

  
} // namespace HTTP