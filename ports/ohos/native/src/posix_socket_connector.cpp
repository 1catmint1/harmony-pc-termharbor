#include "posix_socket_connector.h"

#ifndef _WIN32

#include <arpa/inet.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sstream>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace teraterm::ohos {
namespace {

bool SetNonBlocking(int fd, std::string* error) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    if (error != nullptr) {
      *error = LastSocketError("fcntl(F_GETFL)");
    }
    return false;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    if (error != nullptr) {
      *error = LastSocketError("fcntl(F_SETFL)");
    }
    return false;
  }
  return true;
}

bool FinishConnect(int fd, int timeout_ms, std::string* error) {
  if (!WaitForSocketWritable(fd, timeout_ms, error)) {
    return false;
  }

  int socket_error = 0;
  socklen_t socket_error_size = sizeof(socket_error);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_size) < 0) {
    if (error != nullptr) {
      *error = LastSocketError("getsockopt(SO_ERROR)");
    }
    return false;
  }
  if (socket_error != 0) {
    if (error != nullptr) {
      *error = std::strerror(socket_error);
    }
    return false;
  }
  return true;
}

int ConnectDirectSocket(const std::string& host, uint16_t port, int timeout_ms, std::string* error) {
  addrinfo hints = {};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;

  const std::string port_text = std::to_string(port);
  addrinfo* addresses = nullptr;
  const int gai = getaddrinfo(host.c_str(), port_text.c_str(), &hints, &addresses);
  if (gai != 0) {
    if (error != nullptr) {
      *error = gai_strerror(gai);
    }
    return -1;
  }

  std::string last_error;
  for (addrinfo* item = addresses; item != nullptr; item = item->ai_next) {
    const int fd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
    if (fd < 0) {
      last_error = LastSocketError("socket");
      continue;
    }

    if (!SetNonBlocking(fd, &last_error)) {
      close(fd);
      continue;
    }

    int no_delay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay));

    const int result = connect(fd, item->ai_addr, item->ai_addrlen);
    if (result == 0 || (errno == EINPROGRESS && FinishConnect(fd, timeout_ms, &last_error))) {
      freeaddrinfo(addresses);
      return fd;
    }

    if (result != 0 && errno != EINPROGRESS) {
      last_error = LastSocketError("connect");
    }
    close(fd);
  }

  freeaddrinfo(addresses);
  if (error != nullptr) {
    *error = last_error.empty() ? "unable to connect" : last_error;
  }
  return -1;
}

bool WriteAll(int fd, const uint8_t* data, std::size_t size, int timeout_ms, std::string* error) {
  std::size_t sent = 0;
  while (sent < size) {
    const ssize_t bytes = send(fd, data + sent, size - sent, MSG_NOSIGNAL);
    if (bytes > 0) {
      sent += static_cast<std::size_t>(bytes);
      continue;
    }
    if (bytes < 0 && errno == EINTR) {
      continue;
    }
    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (!WaitForSocketWritable(fd, timeout_ms, error)) {
        return false;
      }
      continue;
    }
    if (error != nullptr) {
      *error = LastSocketError("send");
    }
    return false;
  }
  return true;
}

bool WriteAll(int fd, const std::string& text, int timeout_ms, std::string* error) {
  return WriteAll(fd, reinterpret_cast<const uint8_t*>(text.data()), text.size(), timeout_ms, error);
}

bool WriteAll(int fd, const std::vector<uint8_t>& data, int timeout_ms, std::string* error) {
  return WriteAll(fd, data.data(), data.size(), timeout_ms, error);
}

bool ReadExact(int fd, std::size_t size, std::vector<uint8_t>* output, int timeout_ms, std::string* error) {
  output->assign(size, 0);
  std::size_t received = 0;
  while (received < size) {
    if (!WaitForSocketReadable(fd, timeout_ms, error)) {
      return false;
    }
    const ssize_t bytes = recv(fd, output->data() + received, size - received, 0);
    if (bytes > 0) {
      received += static_cast<std::size_t>(bytes);
      continue;
    }
    if (bytes == 0) {
      if (error != nullptr) {
        *error = "proxy closed the connection";
      }
      return false;
    }
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
      continue;
    }
    if (error != nullptr) {
      *error = LastSocketError("recv");
    }
    return false;
  }
  return true;
}

std::string Base64Encode(const unsigned char* data, std::size_t size) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string output;
  output.reserve(((size + 2) / 3) * 4);
  for (std::size_t i = 0; i < size; i += 3) {
    const uint32_t octet_a = data[i];
    const uint32_t octet_b = i + 1 < size ? data[i + 1] : 0;
    const uint32_t octet_c = i + 2 < size ? data[i + 2] : 0;
    const uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
    output.push_back(kAlphabet[(triple >> 18) & 0x3f]);
    output.push_back(kAlphabet[(triple >> 12) & 0x3f]);
    output.push_back(i + 1 < size ? kAlphabet[(triple >> 6) & 0x3f] : '=');
    output.push_back(i + 2 < size ? kAlphabet[triple & 0x3f] : '=');
  }
  return output;
}

std::string Authority(const std::string& host, uint16_t port) {
  std::ostringstream stream;
  if (host.find(':') != std::string::npos && (host.empty() || host.front() != '[')) {
    stream << "[" << host << "]:" << port;
  } else {
    stream << host << ":" << port;
  }
  return stream.str();
}

bool PerformHttpConnect(int fd, const TransportOpenOptions& options, std::string* error) {
  const std::string authority = Authority(options.host, options.port);
  std::ostringstream request;
  request << "CONNECT " << authority << " HTTP/1.1\r\n";
  request << "Host: " << authority << "\r\n";
  request << "Proxy-Connection: keep-alive\r\n";
  if (!options.proxy.username.empty()) {
    const std::string credential = options.proxy.username + ":" + options.proxy.password;
    request << "Proxy-Authorization: Basic "
            << Base64Encode(reinterpret_cast<const unsigned char*>(credential.data()), credential.size())
            << "\r\n";
  }
  request << "\r\n";

  if (!WriteAll(fd, request.str(), options.timeout_ms, error)) {
    return false;
  }

  std::string response;
  while (response.find("\r\n\r\n") == std::string::npos) {
    if (response.size() > 16384) {
      if (error != nullptr) {
        *error = "HTTP proxy response header is too large";
      }
      return false;
    }
    if (!WaitForSocketReadable(fd, options.timeout_ms, error)) {
      return false;
    }
    char ch = '\0';
    const ssize_t bytes = recv(fd, &ch, 1, 0);
    if (bytes > 0) {
      response.push_back(ch);
      continue;
    }
    if (bytes == 0) {
      if (error != nullptr) {
        *error = "HTTP proxy closed the connection";
      }
      return false;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      continue;
    }
    if (error != nullptr) {
      *error = LastSocketError("recv");
    }
    return false;
  }

  const std::size_t line_end = response.find("\r\n");
  const std::string status_line = line_end == std::string::npos ? response : response.substr(0, line_end);
  const std::size_t first_space = status_line.find(' ');
  if (status_line.rfind("HTTP/", 0) != 0 || first_space == std::string::npos) {
    if (error != nullptr) {
      *error = "HTTP proxy returned an invalid response";
    }
    return false;
  }

  const int status_code = std::atoi(status_line.c_str() + first_space + 1);
  if (status_code < 200 || status_code >= 300) {
    if (error != nullptr) {
      std::ostringstream stream;
      stream << "HTTP proxy CONNECT failed with status " << status_code;
      *error = stream.str();
    }
    return false;
  }
  return true;
}

std::string StripBrackets(const std::string& host) {
  if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
    return host.substr(1, host.size() - 2);
  }
  return host;
}

bool PerformSocks5Connect(int fd, const TransportOpenOptions& options, std::string* error) {
  std::vector<uint8_t> greeting;
  if (options.proxy.username.empty()) {
    greeting = {0x05, 0x01, 0x00};
  } else {
    greeting = {0x05, 0x02, 0x00, 0x02};
  }
  if (!WriteAll(fd, greeting, options.timeout_ms, error)) {
    return false;
  }

  std::vector<uint8_t> response;
  if (!ReadExact(fd, 2, &response, options.timeout_ms, error)) {
    return false;
  }
  if (response[0] != 0x05 || response[1] == 0xff) {
    if (error != nullptr) {
      *error = "SOCKS5 proxy did not accept an authentication method";
    }
    return false;
  }

  if (response[1] == 0x02) {
    if (options.proxy.username.size() > 255 || options.proxy.password.size() > 255) {
      if (error != nullptr) {
        *error = "SOCKS5 proxy username/password are too long";
      }
      return false;
    }
    std::vector<uint8_t> auth = {0x01, static_cast<uint8_t>(options.proxy.username.size())};
    auth.insert(auth.end(), options.proxy.username.begin(), options.proxy.username.end());
    auth.push_back(static_cast<uint8_t>(options.proxy.password.size()));
    auth.insert(auth.end(), options.proxy.password.begin(), options.proxy.password.end());
    if (!WriteAll(fd, auth, options.timeout_ms, error)) {
      return false;
    }
    if (!ReadExact(fd, 2, &response, options.timeout_ms, error)) {
      return false;
    }
    if (response[0] != 0x01 || response[1] != 0x00) {
      if (error != nullptr) {
        *error = "SOCKS5 proxy authentication failed";
      }
      return false;
    }
  } else if (response[1] != 0x00) {
    if (error != nullptr) {
      *error = "SOCKS5 proxy selected an unsupported authentication method";
    }
    return false;
  }

  const std::string host = StripBrackets(options.host);
  std::vector<uint8_t> request = {0x05, 0x01, 0x00};
  in_addr ipv4 = {};
  in6_addr ipv6 = {};
  if (inet_pton(AF_INET, host.c_str(), &ipv4) == 1) {
    request.push_back(0x01);
    const auto* bytes = reinterpret_cast<const uint8_t*>(&ipv4);
    request.insert(request.end(), bytes, bytes + 4);
  } else if (inet_pton(AF_INET6, host.c_str(), &ipv6) == 1) {
    request.push_back(0x04);
    const auto* bytes = reinterpret_cast<const uint8_t*>(&ipv6);
    request.insert(request.end(), bytes, bytes + 16);
  } else {
    if (host.size() > 255) {
      if (error != nullptr) {
        *error = "SOCKS5 target host name is too long";
      }
      return false;
    }
    request.push_back(0x03);
    request.push_back(static_cast<uint8_t>(host.size()));
    request.insert(request.end(), host.begin(), host.end());
  }
  request.push_back(static_cast<uint8_t>((options.port >> 8) & 0xff));
  request.push_back(static_cast<uint8_t>(options.port & 0xff));
  if (!WriteAll(fd, request, options.timeout_ms, error)) {
    return false;
  }

  if (!ReadExact(fd, 4, &response, options.timeout_ms, error)) {
    return false;
  }
  if (response[0] != 0x05) {
    if (error != nullptr) {
      *error = "SOCKS5 proxy returned an invalid response";
    }
    return false;
  }
  if (response[1] != 0x00) {
    if (error != nullptr) {
      std::ostringstream stream;
      stream << "SOCKS5 proxy CONNECT failed with status " << static_cast<int>(response[1]);
      *error = stream.str();
    }
    return false;
  }

  std::size_t bound_length = 0;
  if (response[3] == 0x01) {
    bound_length = 4;
  } else if (response[3] == 0x04) {
    bound_length = 16;
  } else if (response[3] == 0x03) {
    std::vector<uint8_t> length;
    if (!ReadExact(fd, 1, &length, options.timeout_ms, error)) {
      return false;
    }
    bound_length = length[0];
  } else {
    if (error != nullptr) {
      *error = "SOCKS5 proxy returned an unsupported address type";
    }
    return false;
  }
  if (bound_length > 0 && !ReadExact(fd, bound_length, &response, options.timeout_ms, error)) {
    return false;
  }
  return ReadExact(fd, 2, &response, options.timeout_ms, error);
}

bool PerformSocks4Connect(int fd, const TransportOpenOptions& options, std::string* error) {
  const std::string host = StripBrackets(options.host);
  if (host.find('\0') != std::string::npos || options.proxy.username.find('\0') != std::string::npos) {
    if (error != nullptr) {
      *error = "SOCKS4 target host and username cannot contain NUL bytes";
    }
    return false;
  }

  std::vector<uint8_t> request = {
      0x04,
      0x01,
      static_cast<uint8_t>((options.port >> 8) & 0xff),
      static_cast<uint8_t>(options.port & 0xff),
  };

  in_addr ipv4 = {};
  in6_addr ipv6 = {};
  if (inet_pton(AF_INET, host.c_str(), &ipv4) == 1) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&ipv4);
    request.insert(request.end(), bytes, bytes + 4);
  } else if (inet_pton(AF_INET6, host.c_str(), &ipv6) == 1) {
    if (error != nullptr) {
      *error = "SOCKS4 proxy cannot connect to IPv6 literals";
    }
    return false;
  } else {
    request.insert(request.end(), {0x00, 0x00, 0x00, 0x01});
  }

  request.insert(request.end(), options.proxy.username.begin(), options.proxy.username.end());
  request.push_back(0x00);
  if (inet_pton(AF_INET, host.c_str(), &ipv4) != 1) {
    request.insert(request.end(), host.begin(), host.end());
    request.push_back(0x00);
  }

  if (!WriteAll(fd, request, options.timeout_ms, error)) {
    return false;
  }

  std::vector<uint8_t> response;
  if (!ReadExact(fd, 8, &response, options.timeout_ms, error)) {
    return false;
  }
  if (response[1] != 0x5a) {
    if (error != nullptr) {
      std::ostringstream stream;
      stream << "SOCKS4 proxy CONNECT failed with status " << static_cast<int>(response[1]);
      *error = stream.str();
    }
    return false;
  }
  return true;
}

enum class TelnetProxyPrompt {
  Host,
  Username,
  Password,
  Connected,
  Error,
};

bool ContainsPrompt(const std::string& buffer, const std::string& prompt) {
  return !prompt.empty() && buffer.find(prompt) != std::string::npos;
}

bool ReadTelnetProxyPrompt(int fd,
                           const ProxyOpenOptions& proxy,
                           int timeout_ms,
                           TelnetProxyPrompt* prompt,
                           std::string* error) {
  std::string buffer;
  buffer.reserve(1024);
  while (buffer.size() < 16384) {
    if (!WaitForSocketReadable(fd, timeout_ms, error)) {
      return false;
    }
    char ch = '\0';
    const ssize_t bytes = recv(fd, &ch, 1, 0);
    if (bytes > 0) {
      buffer.push_back(ch);
      if (ContainsPrompt(buffer, proxy.telnet_hostname_prompt)) {
        *prompt = TelnetProxyPrompt::Host;
        return true;
      }
      if (ContainsPrompt(buffer, proxy.telnet_username_prompt)) {
        *prompt = TelnetProxyPrompt::Username;
        return true;
      }
      if (ContainsPrompt(buffer, proxy.telnet_password_prompt)) {
        *prompt = TelnetProxyPrompt::Password;
        return true;
      }
      if (ContainsPrompt(buffer, proxy.telnet_connected_message)) {
        *prompt = TelnetProxyPrompt::Connected;
        return true;
      }
      if (ContainsPrompt(buffer, proxy.telnet_error_message)) {
        *prompt = TelnetProxyPrompt::Error;
        return true;
      }
      continue;
    }
    if (bytes == 0) {
      if (error != nullptr) {
        *error = "TELNET proxy closed the connection";
      }
      return false;
    }
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
      continue;
    }
    if (error != nullptr) {
      *error = LastSocketError("recv");
    }
    return false;
  }
  if (error != nullptr) {
    *error = "TELNET proxy prompt response is too large";
  }
  return false;
}

bool PerformTelnetProxyConnect(int fd, const TransportOpenOptions& options, std::string* error) {
  for (int steps = 0; steps < 8; ++steps) {
    TelnetProxyPrompt prompt = TelnetProxyPrompt::Error;
    if (!ReadTelnetProxyPrompt(fd, options.proxy, options.timeout_ms, &prompt, error)) {
      return false;
    }
    switch (prompt) {
      case TelnetProxyPrompt::Host: {
        const std::string authority = Authority(options.host, options.port) + "\n";
        if (!WriteAll(fd, authority, options.timeout_ms, error)) {
          return false;
        }
        break;
      }
      case TelnetProxyPrompt::Username:
        if (!WriteAll(fd, options.proxy.username + "\n", options.timeout_ms, error)) {
          return false;
        }
        break;
      case TelnetProxyPrompt::Password:
        if (!WriteAll(fd, options.proxy.password + "\n", options.timeout_ms, error)) {
          return false;
        }
        break;
      case TelnetProxyPrompt::Connected:
        return true;
      case TelnetProxyPrompt::Error:
        if (error != nullptr) {
          *error = "TELNET proxy rejected the connection";
        }
        return false;
    }
  }

  if (error != nullptr) {
    *error = "TELNET proxy did not finish negotiation";
  }
  return false;
}

}  // namespace

std::string LastSocketError(const char* action) {
  std::ostringstream stream;
  stream << action << ": " << std::strerror(errno);
  return stream.str();
}

bool WaitForSocketWritable(int fd, int timeout_ms, std::string* error) {
  fd_set write_set;
  FD_ZERO(&write_set);
  FD_SET(fd, &write_set);

  timeval timeout;
  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;

  const int result = select(fd + 1, nullptr, &write_set, nullptr, &timeout);
  if (result > 0) {
    return true;
  }
  if (error != nullptr) {
    *error = result == 0 ? "socket write timeout" : LastSocketError("select");
  }
  return false;
}

bool WaitForSocketReadable(int fd, int timeout_ms, std::string* error) {
  fd_set read_set;
  FD_ZERO(&read_set);
  FD_SET(fd, &read_set);

  timeval timeout;
  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;

  const int result = select(fd + 1, &read_set, nullptr, nullptr, &timeout);
  if (result > 0) {
    return true;
  }
  if (error != nullptr) {
    *error = result == 0 ? "socket read timeout" : LastSocketError("select");
  }
  return false;
}

int ConnectTcpSocket(const TransportOpenOptions& options, std::string* error) {
  if (options.port == 0) {
    if (error != nullptr) {
      *error = "target port is required";
    }
    return -1;
  }

  if (options.proxy.type == ProxyType::None) {
    return ConnectDirectSocket(options.host, options.port, options.timeout_ms, error);
  }

  if (options.proxy.host.empty() || options.proxy.port == 0) {
    if (error != nullptr) {
      *error = ProxyTypeName(options.proxy.type) + " proxy host and port are required";
    }
    return -1;
  }

  int fd = ConnectDirectSocket(options.proxy.host, options.proxy.port, options.timeout_ms, error);
  if (fd < 0) {
    return -1;
  }

  bool connected = false;
  if (options.proxy.type == ProxyType::HttpConnect) {
    connected = PerformHttpConnect(fd, options, error);
  } else if (options.proxy.type == ProxyType::Telnet) {
    connected = PerformTelnetProxyConnect(fd, options, error);
  } else if (options.proxy.type == ProxyType::Socks4) {
    connected = PerformSocks4Connect(fd, options, error);
  } else if (options.proxy.type == ProxyType::Socks5) {
    connected = PerformSocks5Connect(fd, options, error);
  } else if (error != nullptr) {
    *error = "unsupported proxy type";
  }

  if (!connected) {
    close(fd);
    return -1;
  }
  return fd;
}

}  // namespace teraterm::ohos

#else
#error "POSIX socket connector requires a POSIX socket platform."
#endif
