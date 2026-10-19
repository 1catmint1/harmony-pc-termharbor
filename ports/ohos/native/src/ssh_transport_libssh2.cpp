#include "scp_completion.h"
#include "ssh_transport.h"

#include "posix_socket_connector.h"

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sstream>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace teraterm::ohos {
namespace {

std::string LastError(const char* action) {
  std::ostringstream stream;
  stream << action << ": " << std::strerror(errno);
  return stream.str();
}

bool FileIsReadable(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  return input.good();
}

bool SetNonBlocking(int fd, std::string* error) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    if (error != nullptr) {
      *error = LastError("fcntl(F_GETFL)");
    }
    return false;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    if (error != nullptr) {
      *error = LastError("fcntl(F_SETFL)");
    }
    return false;
  }
  return true;
}

bool WaitSocket(int fd, LIBSSH2_SESSION* session, int timeout_ms, std::string* error) {
  timeval timeout;
  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;

  fd_set read_set;
  fd_set write_set;
  FD_ZERO(&read_set);
  FD_ZERO(&write_set);

  const int direction = session == nullptr ? LIBSSH2_SESSION_BLOCK_OUTBOUND :
                                             libssh2_session_block_directions(session);
  if ((direction & LIBSSH2_SESSION_BLOCK_INBOUND) != 0) {
    FD_SET(fd, &read_set);
  }
  if ((direction & LIBSSH2_SESSION_BLOCK_OUTBOUND) != 0) {
    FD_SET(fd, &write_set);
  }

  int result = 0;
  do {
    result = select(fd + 1, &read_set, &write_set, nullptr, &timeout);
  } while (result < 0 && errno == EINTR);
  if (result > 0) {
    return true;
  }
  if (error != nullptr) {
    *error = result == 0 ? "SSH operation timeout" : LastError("select");
  }
  return false;
}

bool FinishConnect(int fd, int timeout_ms, std::string* error) {
  if (!WaitSocket(fd, nullptr, timeout_ms, error)) {
    return false;
  }
  int socket_error = 0;
  socklen_t socket_error_size = sizeof(socket_error);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_size) < 0) {
    if (error != nullptr) {
      *error = LastError("getsockopt(SO_ERROR)");
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

int ConnectSocket(const std::string& host, uint16_t port, int timeout_ms, std::string* error) {
  addrinfo hints = {};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;

  addrinfo* addresses = nullptr;
  const int gai = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &addresses);
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
      last_error = LastError("socket");
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
      last_error = LastError("connect");
    }
    close(fd);
  }

  freeaddrinfo(addresses);
  if (error != nullptr) {
    *error = last_error.empty() ? "unable to connect SSH socket" : last_error;
  }
  return -1;
}

std::string SessionLastError(LIBSSH2_SESSION* session, const char* fallback) {
  char* message = nullptr;
  int length = 0;
  libssh2_session_last_error(session, &message, &length, 0);
  if (message != nullptr && length > 0) {
    return std::string(message, static_cast<std::size_t>(length));
  }
  return fallback;
}

bool RunSshLoop(int fd,
                LIBSSH2_SESSION* session,
                const char* action,
                const std::function<int()>& operation,
                int timeout_ms,
                std::string* error) {
  while (true) {
    const int result = operation();
    if (result == 0) {
      return true;
    }
    if (result != LIBSSH2_ERROR_EAGAIN) {
      if (error != nullptr) {
        *error = std::string(action) + ": " + SessionLastError(session, action);
      }
      return false;
    }
    if (!WaitSocket(fd, session, timeout_ms, error)) {
      return false;
    }
  }
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

std::string KeyTypeName(int hostkey_type) {
  switch (hostkey_type) {
    case LIBSSH2_HOSTKEY_TYPE_RSA:
      return "ssh-rsa";
    case LIBSSH2_HOSTKEY_TYPE_DSS:
      return "ssh-dss";
#ifdef LIBSSH2_HOSTKEY_TYPE_ECDSA_256
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:
      return "ecdsa-sha2-nistp256";
#endif
#ifdef LIBSSH2_HOSTKEY_TYPE_ECDSA_384
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:
      return "ecdsa-sha2-nistp384";
#endif
#ifdef LIBSSH2_HOSTKEY_TYPE_ECDSA_521
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521:
      return "ecdsa-sha2-nistp521";
#endif
#ifdef LIBSSH2_HOSTKEY_TYPE_ED25519
    case LIBSSH2_HOSTKEY_TYPE_ED25519:
      return "ssh-ed25519";
#endif
    default:
      return "unknown";
  }
}

bool IsAcceptedHostKey(const KnownHostMatch& match, KnownHostPolicy policy) {
  if (match.status == KnownHostMatchStatus::Trusted) {
    return true;
  }
  if (match.status == KnownHostMatchStatus::NotFound) {
    return policy == KnownHostPolicy::AcceptNew || policy == KnownHostPolicy::AcceptChanged;
  }
  if (match.status == KnownHostMatchStatus::Changed) {
    return policy == KnownHostPolicy::AcceptChanged;
  }
  return false;
}

bool AuthenticateSession(int fd, LIBSSH2_SESSION* session, const SshConnectionOptions& options, std::string* error) {
  if (!options.private_key_path.empty()) {
    std::string public_key_path = options.public_key_path;
    if (public_key_path.empty()) {
      const std::string inferred_public_key_path = options.private_key_path + ".pub";
      if (FileIsReadable(inferred_public_key_path)) {
        public_key_path = inferred_public_key_path;
      }
    }
    const char* public_key = public_key_path.empty() ? nullptr : public_key_path.c_str();
    return RunSshLoop(fd, session, "SSH public-key authentication failed",
                      [&]() {
                        return libssh2_userauth_publickey_fromfile(
                            session, options.username.c_str(), public_key,
                            options.private_key_path.c_str(),
                            options.passphrase.empty() ? nullptr : options.passphrase.c_str());
                      },
                      options.timeout_ms, error);
  }
  return RunSshLoop(fd, session, "SSH password authentication failed",
                    [&]() {
                      return libssh2_userauth_password(
                          session, options.username.c_str(), options.password.c_str());
                    },
                    options.timeout_ms, error);
}

struct AuthenticatedSession {
  int fd = -1;
  LIBSSH2_SESSION* session = nullptr;

  ~AuthenticatedSession() { Close(); }

  void Close() {
    if (session != nullptr) {
      libssh2_session_disconnect(session, "client disconnect");
      libssh2_session_free(session);
      session = nullptr;
    }
    if (fd >= 0) {
      close(fd);
      fd = -1;
    }
  }
};

bool ConnectAuthenticatedSession(const SshConnectionOptions& options,
                                 AuthenticatedSession* connection,
                                 SshFileTransferResult* result) {
  if (connection == nullptr || result == nullptr) {
    return false;
  }

  std::string error;
  TransportOpenOptions transport_options;
  transport_options.host = options.host;
  transport_options.port = options.port == 0 ? 22 : options.port;
  transport_options.timeout_ms = options.timeout_ms;
  transport_options.proxy = options.proxy;
  connection->fd = ConnectTcpSocket(transport_options, &error);
  if (connection->fd < 0) {
    result->error = error;
    return false;
  }

  connection->session = libssh2_session_init();
  if (connection->session == nullptr) {
    result->error = "libssh2_session_init failed";
    return false;
  }
  libssh2_session_set_blocking(connection->session, 0);

  if (!RunSshLoop(connection->fd, connection->session, "SSH handshake failed",
                  [&]() { return libssh2_session_handshake(connection->session, connection->fd); },
                  options.timeout_ms, &result->error)) {
    return false;
  }

  std::size_t key_length = 0;
  int hostkey_type = 0;
  const char* hostkey = libssh2_session_hostkey(connection->session, &key_length, &hostkey_type);
  if (hostkey == nullptr || key_length == 0) {
    result->error = "server did not provide a host key";
    return false;
  }

  const std::string key_type = KeyTypeName(hostkey_type);
  const std::string key_base64 = Base64Encode(reinterpret_cast<const unsigned char*>(hostkey), key_length);
  const auto known_host_entries = KnownHostsStore::Parse(options.known_hosts_text);
  result->host_key_match = KnownHostsStore::Match(
      known_host_entries, options.host, options.port == 0 ? 22 : options.port, key_type, key_base64);

  if (!IsAcceptedHostKey(result->host_key_match, options.known_host_policy)) {
    result->error = result->host_key_match.message;
    return false;
  }

  if (result->host_key_match.status == KnownHostMatchStatus::NotFound ||
      result->host_key_match.status == KnownHostMatchStatus::Changed) {
    result->updated_known_hosts_text = KnownHostsStore::AddOrUpdate(
        options.known_hosts_text, options.host, options.port == 0 ? 22 : options.port,
        key_type, key_base64, "teraterm-ohos");
  } else {
    result->updated_known_hosts_text = options.known_hosts_text;
  }

  if (!AuthenticateSession(connection->fd, connection->session, options, &result->error)) {
    return false;
  }
  return true;
}

std::string SftpLastError(LIBSSH2_SESSION* session, LIBSSH2_SFTP* sftp, const char* fallback) {
  std::string message = SessionLastError(session, fallback);
  if (sftp != nullptr) {
    const unsigned long sftp_status = libssh2_sftp_last_error(sftp);
    if (sftp_status != 0) {
      std::ostringstream stream;
      stream << message << " (SFTP status " << sftp_status << ")";
      return stream.str();
    }
  }
  return message;
}

LIBSSH2_SFTP* OpenSftpSession(AuthenticatedSession* connection,
                              const SshConnectionOptions& options,
                              SshFileTransferResult* result) {
  if (connection == nullptr || result == nullptr) {
    return nullptr;
  }
  while (true) {
    LIBSSH2_SFTP* sftp = libssh2_sftp_init(connection->session);
    if (sftp != nullptr) {
      return sftp;
    }
    if (libssh2_session_last_errno(connection->session) != LIBSSH2_ERROR_EAGAIN) {
      result->error = SessionLastError(connection->session, "SFTP init failed");
      return nullptr;
    }
    if (!WaitSocket(connection->fd, connection->session, options.timeout_ms, &result->error)) {
      return nullptr;
    }
  }
}

LIBSSH2_SFTP_HANDLE* OpenSftpFile(AuthenticatedSession* connection,
                                  LIBSSH2_SFTP* sftp,
                                  const SshConnectionOptions& options,
                                  const std::string& path,
                                  unsigned long flags,
                                  long mode,
                                  const char* fallback,
                                  SshFileTransferResult* result) {
  if (connection == nullptr || sftp == nullptr || result == nullptr) {
    return nullptr;
  }
  while (true) {
    LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_open_ex(
        sftp, path.c_str(), static_cast<unsigned int>(path.size()), flags, mode, LIBSSH2_SFTP_OPENFILE);
    if (handle != nullptr) {
      return handle;
    }
    if (libssh2_session_last_errno(connection->session) != LIBSSH2_ERROR_EAGAIN) {
      result->error = SftpLastError(connection->session, sftp, fallback);
      return nullptr;
    }
    if (!WaitSocket(connection->fd, connection->session, options.timeout_ms, &result->error)) {
      return nullptr;
    }
  }
}

bool CloseSftpHandle(AuthenticatedSession* connection,
                     const SshConnectionOptions& options,
                     LIBSSH2_SFTP_HANDLE* handle,
                     std::string* error) {
  if (connection == nullptr || handle == nullptr) {
    return true;
  }
  while (true) {
    const int close_result = libssh2_sftp_close_handle(handle);
    if (close_result == 0) {
      return true;
    }
    if (close_result != LIBSSH2_ERROR_EAGAIN) {
      if (error != nullptr) {
        *error = SessionLastError(connection->session, "SFTP handle close failed");
      }
      return false;
    }
    if (!WaitSocket(connection->fd, connection->session, options.timeout_ms, error)) {
      return false;
    }
  }
}

void ShutdownSftpSession(AuthenticatedSession* connection,
                         const SshConnectionOptions& options,
                         LIBSSH2_SFTP* sftp) {
  if (connection == nullptr || sftp == nullptr) {
    return;
  }
  while (libssh2_sftp_shutdown(sftp) == LIBSSH2_ERROR_EAGAIN) {
    std::string ignored_error;
    if (!WaitSocket(connection->fd, connection->session, options.timeout_ms, &ignored_error)) {
      return;
    }
  }
}

uint16_t SocketPort(const sockaddr* address) {
  if (address->sa_family == AF_INET) {
    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
    return ntohs(ipv4->sin_port);
  }
  if (address->sa_family == AF_INET6) {
    const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(address);
    return ntohs(ipv6->sin6_port);
  }
  return 0;
}

int CreateLocalListener(const std::string& local_host,
                        uint16_t local_port,
                        uint16_t* bound_port,
                        std::string* error) {
  addrinfo hints = {};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  hints.ai_flags = local_host.empty() ? AI_PASSIVE : 0;

  addrinfo* addresses = nullptr;
  const std::string port_text = std::to_string(local_port);
  const char* host = local_host.empty() ? nullptr : local_host.c_str();
  const int gai = getaddrinfo(host, port_text.c_str(), &hints, &addresses);
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
      last_error = LastError("socket");
      continue;
    }

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (bind(fd, item->ai_addr, item->ai_addrlen) == 0 && listen(fd, 8) == 0 &&
        SetNonBlocking(fd, &last_error)) {
      sockaddr_storage bound_address = {};
      socklen_t bound_size = sizeof(bound_address);
      if (getsockname(fd, reinterpret_cast<sockaddr*>(&bound_address), &bound_size) == 0 &&
          bound_port != nullptr) {
        *bound_port = SocketPort(reinterpret_cast<sockaddr*>(&bound_address));
      }
      freeaddrinfo(addresses);
      return fd;
    }

    last_error = LastError("bind/listen");
    close(fd);
  }

  freeaddrinfo(addresses);
  if (error != nullptr) {
    *error = last_error.empty() ? "unable to listen for SSH port forward" : last_error;
  }
  return -1;
}

bool WaitFdWritable(int fd, int timeout_ms, std::string* error) {
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
    *error = result == 0 ? "socket write timeout" : LastError("select");
  }
  return false;
}

bool WriteSocketAll(int fd, const char* data, std::size_t size, std::string* error) {
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
      if (!WaitFdWritable(fd, 5000, error)) {
        return false;
      }
      continue;
    }
    if (error != nullptr) {
      *error = LastError("send");
    }
    return false;
  }
  return true;
}

class Libssh2LocalPortForwardHandle final : public SshPortForwardHandle {
public:
  ~Libssh2LocalPortForwardHandle() override { Stop(); }

  bool Start(const SshPortForwardOptions& options, SshPortForwardStartResult* result) {
    if (result == nullptr) {
      return false;
    }
    options_ = options;
    if (options_.local_host.empty()) {
      options_.local_host = "127.0.0.1";
    }
    if (options_.remote_host.empty() || options_.remote_port == 0) {
      result->error = "remote forward host and port are required";
      return false;
    }

    std::string error;
    listen_fd_ = CreateLocalListener(options_.local_host, options_.local_port, &bound_port_, &error);
    if (listen_fd_ < 0) {
      result->error = error;
      return false;
    }

    SshFileTransferResult auth_result;
    if (!ConnectAuthenticatedSession(options_.connection, &connection_, &auth_result)) {
      result->error = auth_result.error;
      result->host_key_match = auth_result.host_key_match;
      result->updated_known_hosts_text = auth_result.updated_known_hosts_text;
      Stop();
      return false;
    }

    result->started = true;
    result->bound_port = bound_port_;
    result->host_key_match = auth_result.host_key_match;
    result->updated_known_hosts_text = auth_result.updated_known_hosts_text;
    worker_ = std::thread([this]() { AcceptLoop(); });
    return true;
  }

  void Stop() override {
    stop_ = true;
    if (listen_fd_ >= 0) {
      shutdown(listen_fd_, SHUT_RDWR);
      close(listen_fd_);
      listen_fd_ = -1;
    }
    if (worker_.joinable()) {
      worker_.join();
    }
    connection_.Close();
  }

  uint16_t BoundPort() const override { return bound_port_; }

private:
  void AcceptLoop() {
    while (!stop_) {
      sockaddr_storage peer_address = {};
      socklen_t peer_size = sizeof(peer_address);
      const int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer_address), &peer_size);
      if (client_fd < 0) {
        if (errno == EINTR) {
          continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EBADF || errno == EINVAL) {
          WaitForClientOrStop();
          continue;
        }
        break;
      }
      std::string ignored_error;
      SetNonBlocking(client_fd, &ignored_error);
      ForwardClient(client_fd);
      close(client_fd);
    }
  }

  void WaitForClientOrStop() {
    if (listen_fd_ < 0) {
      return;
    }
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(listen_fd_, &read_set);
    timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 200000;
    select(listen_fd_ + 1, &read_set, nullptr, nullptr, &timeout);
  }

  LIBSSH2_CHANNEL* OpenDirectTcpipChannel() {
    while (!stop_) {
      LIBSSH2_CHANNEL* channel = libssh2_channel_direct_tcpip_ex(
          connection_.session, options_.remote_host.c_str(), options_.remote_port,
          options_.local_host.c_str(), bound_port_);
      if (channel != nullptr) {
        return channel;
      }
      if (libssh2_session_last_errno(connection_.session) != LIBSSH2_ERROR_EAGAIN) {
        return nullptr;
      }
      std::string ignored_error;
      if (!WaitSocket(connection_.fd, connection_.session, options_.connection.timeout_ms, &ignored_error)) {
        return nullptr;
      }
    }
    return nullptr;
  }

  bool WriteChannelAll(LIBSSH2_CHANNEL* channel, const char* data, std::size_t size) {
    std::size_t written = 0;
    while (!stop_ && written < size) {
      const ssize_t bytes = libssh2_channel_write(channel, data + written, size - written);
      if (bytes > 0) {
        written += static_cast<std::size_t>(bytes);
        continue;
      }
      if (bytes == LIBSSH2_ERROR_EAGAIN) {
        std::string ignored_error;
        if (!WaitSocket(connection_.fd, connection_.session, 5000, &ignored_error)) {
          return false;
        }
        continue;
      }
      return false;
    }
    return written == size;
  }

  void ForwardClient(int client_fd) {
    LIBSSH2_CHANNEL* channel = OpenDirectTcpipChannel();
    if (channel == nullptr) {
      return;
    }

    std::vector<char> buffer(32768);
    while (!stop_) {
      bool progressed = false;
      bool close_channel = false;

      const ssize_t client_bytes = recv(client_fd, buffer.data(), buffer.size(), 0);
      if (client_bytes > 0) {
        progressed = true;
        if (!WriteChannelAll(channel, buffer.data(), static_cast<std::size_t>(client_bytes))) {
          break;
        }
      } else if (client_bytes == 0) {
        break;
      } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        break;
      }

      while (!stop_) {
        const ssize_t ssh_bytes = libssh2_channel_read(channel, buffer.data(), buffer.size());
        if (ssh_bytes > 0) {
          progressed = true;
          std::string ignored_error;
          if (!WriteSocketAll(client_fd, buffer.data(), static_cast<std::size_t>(ssh_bytes), &ignored_error)) {
            close_channel = true;
            break;
          }
          continue;
        }
        if (ssh_bytes == LIBSSH2_ERROR_EAGAIN) {
          break;
        }
        if (ssh_bytes == 0 && libssh2_channel_eof(channel) != 0) {
          close_channel = true;
        }
        break;
      }

      if (close_channel) {
        break;
      }
      if (!progressed) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(client_fd, &read_set);
        FD_SET(connection_.fd, &read_set);
        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        const int max_fd = std::max(client_fd, connection_.fd);
        select(max_fd + 1, &read_set, nullptr, nullptr, &timeout);
      }
    }

    libssh2_channel_close(channel);
    libssh2_channel_free(channel);
  }

  SshPortForwardOptions options_;
  AuthenticatedSession connection_;
  std::atomic<bool> stop_{false};
  int listen_fd_ = -1;
  uint16_t bound_port_ = 0;
  std::thread worker_;
};

}  // namespace

class Libssh2SessionTransport final : public SshSessionTransport {
public:
  ~Libssh2SessionTransport() override { Close(); }

  bool Connect(const TransportOpenOptions&, std::string* error) override {
    if (error != nullptr) {
      *error = "SSH transport uses ConnectSsh";
    }
    return false;
  }

  bool ConnectSsh(const SshConnectionOptions& options, SshConnectionResult* result) override {
    Close();
    if (result == nullptr) {
      return false;
    }
    result->connected = false;

    std::string error;
    TransportOpenOptions transport_options;
    transport_options.host = options.host;
    transport_options.port = options.port == 0 ? 22 : options.port;
    transport_options.timeout_ms = options.timeout_ms;
    transport_options.proxy = options.proxy;
    fd_ = ConnectTcpSocket(transport_options, &error);
    if (fd_ < 0) {
      result->error = error;
      return false;
    }

    session_ = libssh2_session_init();
    if (session_ == nullptr) {
      result->error = "libssh2_session_init failed";
      Close();
      return false;
    }
    libssh2_session_set_blocking(session_, 0);

    if (!RunSshLoop(fd_, session_, "SSH handshake failed",
                    [&]() { return libssh2_session_handshake(session_, fd_); },
                    options.timeout_ms, &result->error)) {
      Close();
      return false;
    }

    std::size_t key_length = 0;
    int hostkey_type = 0;
    const char* hostkey = libssh2_session_hostkey(session_, &key_length, &hostkey_type);
    if (hostkey == nullptr || key_length == 0) {
      result->error = "server did not provide a host key";
      Close();
      return false;
    }

    result->key_type = KeyTypeName(hostkey_type);
    result->key_base64 = Base64Encode(reinterpret_cast<const unsigned char*>(hostkey), key_length);
    const auto known_host_entries = KnownHostsStore::Parse(options.known_hosts_text);
    result->host_key_match = KnownHostsStore::Match(
        known_host_entries, options.host, options.port == 0 ? 22 : options.port,
        result->key_type, result->key_base64);

    if (!IsAcceptedHostKey(result->host_key_match, options.known_host_policy)) {
      result->error = result->host_key_match.message;
      Close();
      return false;
    }

    if (result->host_key_match.status == KnownHostMatchStatus::NotFound ||
        result->host_key_match.status == KnownHostMatchStatus::Changed) {
      result->updated_known_hosts_text = KnownHostsStore::AddOrUpdate(
          options.known_hosts_text, options.host, options.port == 0 ? 22 : options.port,
          result->key_type, result->key_base64, "teraterm-ohos");
    } else {
      result->updated_known_hosts_text = options.known_hosts_text;
    }

    if (!Authenticate(options, &result->error)) {
      Close();
      return false;
    }

    if (!OpenShell(options, &result->error)) {
      Close();
      return false;
    }

    result->connected = true;
    return true;
  }

  void Close() override {
    if (channel_ != nullptr) {
      libssh2_channel_close(channel_);
      libssh2_channel_free(channel_);
      channel_ = nullptr;
    }
    if (session_ != nullptr) {
      libssh2_session_disconnect(session_, "client disconnect");
      libssh2_session_free(session_);
      session_ = nullptr;
    }
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }
  }

  bool IsOpen() const override { return channel_ != nullptr && session_ != nullptr && fd_ >= 0; }

  TransportReadResult Read(std::size_t max_bytes) override {
    TransportReadResult result;
    if (!IsOpen()) {
      result.status = TransportReadStatus::Closed;
      return result;
    }

    std::vector<uint8_t> buffer(max_bytes == 0 ? 4096 : max_bytes);
    const ssize_t bytes = libssh2_channel_read(channel_, reinterpret_cast<char*>(buffer.data()), buffer.size());
    if (bytes > 0) {
      buffer.resize(static_cast<std::size_t>(bytes));
      result.status = TransportReadStatus::Data;
      result.data = std::move(buffer);
      return result;
    }
    if (bytes == LIBSSH2_ERROR_EAGAIN) {
      result.status = TransportReadStatus::WouldBlock;
      return result;
    }
    if (bytes == 0 && libssh2_channel_eof(channel_) != 0) {
      Close();
      result.status = TransportReadStatus::Closed;
      return result;
    }
    if (bytes == 0) {
      result.status = TransportReadStatus::WouldBlock;
      return result;
    }

    result.status = TransportReadStatus::Error;
    result.error = SessionLastError(session_, "SSH read failed");
    Close();
    return result;
  }

  bool Write(const uint8_t* data, std::size_t size, std::string* error) override {
    if (!IsOpen()) {
      if (error != nullptr) {
        *error = "SSH channel is closed";
      }
      return false;
    }

    std::size_t written = 0;
    while (written < size) {
      const ssize_t bytes = libssh2_channel_write(
          channel_, reinterpret_cast<const char*>(data + written), size - written);
      if (bytes > 0) {
        written += static_cast<std::size_t>(bytes);
        continue;
      }
      if (bytes == LIBSSH2_ERROR_EAGAIN) {
        if (!WaitSocket(fd_, session_, 5000, error)) {
          return false;
        }
        continue;
      }
      if (error != nullptr) {
        *error = SessionLastError(session_, "SSH write failed");
      }
      Close();
      return false;
    }
    return true;
  }

  void ResizePty(std::size_t rows, std::size_t cols) override {
    if (IsOpen()) {
      libssh2_channel_request_pty_size_ex(channel_, static_cast<int>(cols), static_cast<int>(rows), 0, 0);
    }
  }

private:
  bool Authenticate(const SshConnectionOptions& options, std::string* error) {
    return AuthenticateSession(fd_, session_, options, error);
  }

  bool OpenShell(const SshConnectionOptions& options, std::string* error) {
    while (true) {
      channel_ = libssh2_channel_open_session(session_);
      if (channel_ != nullptr) {
        break;
      }
      if (libssh2_session_last_errno(session_) != LIBSSH2_ERROR_EAGAIN) {
        if (error != nullptr) {
          *error = SessionLastError(session_, "SSH channel open failed");
        }
        return false;
      }
      if (!WaitSocket(fd_, session_, options.timeout_ms, error)) {
        return false;
      }
    }

    if (!RunSshLoop(fd_, session_, "SSH pty request failed",
                    [&]() {
                      return libssh2_channel_request_pty_ex(
                          channel_, options.terminal_type.c_str(), options.terminal_type.size(),
                          nullptr, 0, static_cast<int>(options.cols), static_cast<int>(options.rows), 0, 0);
                    },
                    options.timeout_ms, error)) {
      return false;
    }

    return RunSshLoop(fd_, session_, "SSH shell request failed",
                      [&]() { return libssh2_channel_shell(channel_); },
                      options.timeout_ms, error);
  }

  int fd_ = -1;
  LIBSSH2_SESSION* session_ = nullptr;
  LIBSSH2_CHANNEL* channel_ = nullptr;
};

std::unique_ptr<SshSessionTransport> CreateSshSessionTransport() {
  return std::make_unique<Libssh2SessionTransport>();
}

SshFileTransferResult UploadScpFileDirect(const SshFileTransferOptions& options) {
  SshFileTransferResult result;
  if (options.local_path.empty() || options.remote_path.empty()) {
    result.error = "local and remote paths are required";
    return result;
  }

  std::ifstream input(options.local_path, std::ios::binary);
  if (!input.is_open()) {
    result.error = "local file could not be opened for upload";
    return result;
  }
  input.seekg(0, std::ios::end);
  const std::streamoff file_size = input.tellg();
  if (file_size < 0) {
    result.error = "local file size could not be read";
    return result;
  }
  input.seekg(0, std::ios::beg);

  AuthenticatedSession connection;
  if (!ConnectAuthenticatedSession(options.connection, &connection, &result)) {
    return result;
  }

  LIBSSH2_CHANNEL* channel = nullptr;
  while (channel == nullptr) {
    channel = libssh2_scp_send64(connection.session, options.remote_path.c_str(),
                                 options.mode, static_cast<libssh2_uint64_t>(file_size), 0, 0);
    if (channel != nullptr) {
      break;
    }
    if (libssh2_session_last_errno(connection.session) != LIBSSH2_ERROR_EAGAIN) {
      result.error = SessionLastError(connection.session, "SCP upload open failed");
      return result;
    }
    if (!WaitSocket(connection.fd, connection.session, options.connection.timeout_ms, &result.error)) {
      return result;
    }
  }

  std::vector<char> buffer(32768);
  while (input.good()) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read_count = input.gcount();
    std::size_t written = 0;
    while (written < static_cast<std::size_t>(read_count)) {
      const ssize_t bytes = libssh2_channel_write(channel, buffer.data() + written,
                                                  static_cast<std::size_t>(read_count) - written);
      if (bytes > 0) {
        written += static_cast<std::size_t>(bytes);
        result.bytes_transferred += static_cast<std::size_t>(bytes);
        continue;
      }
      if (bytes == LIBSSH2_ERROR_EAGAIN) {
        if (!WaitSocket(connection.fd, connection.session, options.connection.timeout_ms, &result.error)) {
          libssh2_channel_free(channel);
          return result;
        }
        continue;
      }
      result.error = SessionLastError(connection.session, "SCP upload write failed");
      libssh2_channel_free(channel);
      return result;
    }
  }

  if (!CompleteScpFile(true,
      [&](char* byte,bool writing)->long { return writing ? libssh2_channel_write(channel,byte,1) : libssh2_channel_read(channel,byte,1); },
      [&](){return WaitSocket(connection.fd,connection.session,options.connection.timeout_ms,&result.error);},
      LIBSSH2_ERROR_EAGAIN,&result.error)) {
    libssh2_channel_free(channel);
    return result;
  }
  libssh2_channel_send_eof(channel);
  libssh2_channel_wait_eof(channel);
  libssh2_channel_wait_closed(channel);
  libssh2_channel_free(channel);
  result.success = result.bytes_transferred == static_cast<std::size_t>(file_size);
  if (!result.success) {
    result.error = "SCP upload ended before all bytes were sent";
  }
  return result;
}

SshFileTransferResult DownloadScpFileDirect(const SshFileTransferOptions& options) {
  SshFileTransferResult result;
  if (options.local_path.empty() || options.remote_path.empty()) {
    result.error = "local and remote paths are required";
    return result;
  }

  AuthenticatedSession connection;
  if (!ConnectAuthenticatedSession(options.connection, &connection, &result)) {
    return result;
  }

  libssh2_struct_stat remote_stat = {};
  LIBSSH2_CHANNEL* channel = nullptr;
  while (channel == nullptr) {
    channel = libssh2_scp_recv2(connection.session, options.remote_path.c_str(), &remote_stat);
    if (channel != nullptr) {
      break;
    }
    if (libssh2_session_last_errno(connection.session) != LIBSSH2_ERROR_EAGAIN) {
      result.error = SessionLastError(connection.session, "SCP download open failed");
      return result;
    }
    if (!WaitSocket(connection.fd, connection.session, options.connection.timeout_ms, &result.error)) {
      return result;
    }
  }

  std::ofstream output(options.local_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    result.error = "local file could not be opened for download";
    libssh2_channel_free(channel);
    return result;
  }

  const std::size_t expected_size = static_cast<std::size_t>(remote_stat.st_size);
  std::vector<char> buffer(32768);
  while (result.bytes_transferred < expected_size) {
    const std::size_t remaining = expected_size - result.bytes_transferred;
    const ssize_t bytes = libssh2_channel_read(channel, buffer.data(), std::min(buffer.size(), remaining));
    if (bytes > 0) {
      output.write(buffer.data(), bytes);
      if (!output.good()) {
        result.error = "local file write failed";
        libssh2_channel_free(channel);
        return result;
      }
      result.bytes_transferred += static_cast<std::size_t>(bytes);
      continue;
    }
    if (bytes == LIBSSH2_ERROR_EAGAIN) {
      if (!WaitSocket(connection.fd, connection.session, options.connection.timeout_ms, &result.error)) {
        libssh2_channel_free(channel);
        return result;
      }
      continue;
    }
    if (bytes == 0) {
      result.error = "SCP download ended before all bytes were received";
      libssh2_channel_free(channel);
      return result;
    }
    result.error = SessionLastError(connection.session, "SCP download read failed");
    libssh2_channel_free(channel);
    return result;
  }

  output.close();
  if (!CompleteScpFile(false,
      [&](char* byte,bool writing)->long { return writing ? libssh2_channel_write(channel,byte,1) : libssh2_channel_read(channel,byte,1); },
      [&](){return WaitSocket(connection.fd,connection.session,options.connection.timeout_ms,&result.error);},
      LIBSSH2_ERROR_EAGAIN,&result.error)) {
    libssh2_channel_free(channel);
    return result;
  }
  libssh2_channel_send_eof(channel);
  libssh2_channel_wait_eof(channel);
  libssh2_channel_wait_closed(channel);
  libssh2_channel_free(channel);
  result.success = true;
  return result;
}

SshFileTransferResult UploadScpFile(const SshFileTransferOptions& options) {
  SshFileTransferResult result = UploadScpFileDirect(options);
  if (result.success) {
    return result;
  }
  SshFileTransferResult fallback = UploadSftpFile(options);
  return fallback.success ? fallback : result;
}

SshFileTransferResult DownloadScpFile(const SshFileTransferOptions& options) {
  SshFileTransferResult result = DownloadScpFileDirect(options);
  if (result.success) {
    return result;
  }
  SshFileTransferResult fallback = DownloadSftpFile(options);
  return fallback.success ? fallback : result;
}

SshFileTransferResult UploadSftpFile(const SshFileTransferOptions& options) {
  SshFileTransferResult result;
  if (options.local_path.empty() || options.remote_path.empty()) {
    result.error = "local and remote paths are required";
    return result;
  }

  std::ifstream input(options.local_path, std::ios::binary);
  if (!input.is_open()) {
    result.error = "local file could not be opened for SFTP upload";
    return result;
  }
  input.seekg(0, std::ios::end);
  const std::streamoff file_size = input.tellg();
  if (file_size < 0) {
    result.error = "local file size could not be read";
    return result;
  }
  input.seekg(0, std::ios::beg);

  AuthenticatedSession connection;
  if (!ConnectAuthenticatedSession(options.connection, &connection, &result)) {
    return result;
  }

  LIBSSH2_SFTP* sftp = OpenSftpSession(&connection, options.connection, &result);
  if (sftp == nullptr) {
    return result;
  }

  LIBSSH2_SFTP_HANDLE* handle = OpenSftpFile(
      &connection, sftp, options.connection, options.remote_path,
      LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
      options.mode, "SFTP upload open failed", &result);
  if (handle == nullptr) {
    ShutdownSftpSession(&connection, options.connection, sftp);
    return result;
  }

  std::vector<char> buffer(32768);
  while (input.good()) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read_count = input.gcount();
    std::size_t written = 0;
    while (written < static_cast<std::size_t>(read_count)) {
      const ssize_t bytes = libssh2_sftp_write(handle, buffer.data() + written,
                                               static_cast<std::size_t>(read_count) - written);
      if (bytes > 0) {
        written += static_cast<std::size_t>(bytes);
        result.bytes_transferred += static_cast<std::size_t>(bytes);
        continue;
      }
      if (bytes == 0 || bytes == LIBSSH2_ERROR_EAGAIN) {
        if (!WaitSocket(connection.fd, connection.session, options.connection.timeout_ms, &result.error)) {
          CloseSftpHandle(&connection, options.connection, handle, nullptr);
          ShutdownSftpSession(&connection, options.connection, sftp);
          return result;
        }
        continue;
      }
      result.error = SftpLastError(connection.session, sftp, "SFTP upload write failed");
      CloseSftpHandle(&connection, options.connection, handle, nullptr);
      ShutdownSftpSession(&connection, options.connection, sftp);
      return result;
    }
  }

  const bool closed = CloseSftpHandle(&connection, options.connection, handle, &result.error);
  ShutdownSftpSession(&connection, options.connection, sftp);
  if (!closed) {
    return result;
  }
  result.success = result.bytes_transferred == static_cast<std::size_t>(file_size);
  if (!result.success) {
    result.error = "SFTP upload ended before all bytes were sent";
  }
  return result;
}

SshFileTransferResult DownloadSftpFile(const SshFileTransferOptions& options) {
  SshFileTransferResult result;
  if (options.local_path.empty() || options.remote_path.empty()) {
    result.error = "local and remote paths are required";
    return result;
  }

  AuthenticatedSession connection;
  if (!ConnectAuthenticatedSession(options.connection, &connection, &result)) {
    return result;
  }

  LIBSSH2_SFTP* sftp = OpenSftpSession(&connection, options.connection, &result);
  if (sftp == nullptr) {
    return result;
  }

  LIBSSH2_SFTP_HANDLE* handle = OpenSftpFile(
      &connection, sftp, options.connection, options.remote_path,
      LIBSSH2_FXF_READ, 0, "SFTP download open failed", &result);
  if (handle == nullptr) {
    ShutdownSftpSession(&connection, options.connection, sftp);
    return result;
  }

  std::ofstream output(options.local_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    result.error = "local file could not be opened for SFTP download";
    CloseSftpHandle(&connection, options.connection, handle, nullptr);
    ShutdownSftpSession(&connection, options.connection, sftp);
    return result;
  }

  std::vector<char> buffer(32768);
  while (true) {
    const ssize_t bytes = libssh2_sftp_read(handle, buffer.data(), buffer.size());
    if (bytes > 0) {
      output.write(buffer.data(), bytes);
      if (!output.good()) {
        result.error = "local file write failed";
        CloseSftpHandle(&connection, options.connection, handle, nullptr);
        ShutdownSftpSession(&connection, options.connection, sftp);
        return result;
      }
      result.bytes_transferred += static_cast<std::size_t>(bytes);
      continue;
    }
    if (bytes == LIBSSH2_ERROR_EAGAIN) {
      if (!WaitSocket(connection.fd, connection.session, options.connection.timeout_ms, &result.error)) {
        CloseSftpHandle(&connection, options.connection, handle, nullptr);
        ShutdownSftpSession(&connection, options.connection, sftp);
        return result;
      }
      continue;
    }
    if (bytes == 0) {
      break;
    }
    result.error = SftpLastError(connection.session, sftp, "SFTP download read failed");
    CloseSftpHandle(&connection, options.connection, handle, nullptr);
    ShutdownSftpSession(&connection, options.connection, sftp);
    return result;
  }

  output.close();
  const bool closed = CloseSftpHandle(&connection, options.connection, handle, &result.error);
  ShutdownSftpSession(&connection, options.connection, sftp);
  if (!closed) {
    return result;
  }
  result.success = true;
  return result;
}

std::unique_ptr<SshPortForwardHandle> StartSshLocalPortForward(const SshPortForwardOptions& options,
                                                               SshPortForwardStartResult* result) {
  auto handle = std::make_unique<Libssh2LocalPortForwardHandle>();
  if (!handle->Start(options, result)) {
    return nullptr;
  }
  return handle;
}

KnownHostPolicy KnownHostPolicyFromString(const std::string& value) {
  if (value == "accept_new") {
    return KnownHostPolicy::AcceptNew;
  }
  if (value == "accept_changed") {
    return KnownHostPolicy::AcceptChanged;
  }
  return KnownHostPolicy::Strict;
}

std::string KnownHostPolicyName(KnownHostPolicy policy) {
  switch (policy) {
    case KnownHostPolicy::Strict:
      return "strict";
    case KnownHostPolicy::AcceptNew:
      return "accept_new";
    case KnownHostPolicy::AcceptChanged:
      return "accept_changed";
  }
  return "strict";
}

}  // namespace teraterm::ohos
