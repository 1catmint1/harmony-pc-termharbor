#include "posix_tcp_transport.h"

#ifndef _WIN32

#include "posix_socket_connector.h"

#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace teraterm::ohos {

PosixTcpTransport::PosixTcpTransport() = default;

PosixTcpTransport::~PosixTcpTransport() {
  Close();
}

bool PosixTcpTransport::Connect(const TransportOpenOptions& options, std::string* error) {
  Close();
  fd_ = ConnectTcpSocket(options, error);
  return fd_ >= 0;
}

void PosixTcpTransport::Close() {
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

bool PosixTcpTransport::IsOpen() const {
  return fd_ >= 0;
}

TransportReadResult PosixTcpTransport::Read(std::size_t max_bytes) {
  TransportReadResult result;
  if (fd_ < 0) {
    result.status = TransportReadStatus::Closed;
    return result;
  }

  std::vector<uint8_t> buffer(max_bytes == 0 ? 4096 : max_bytes);
  const ssize_t bytes = recv(fd_, buffer.data(), buffer.size(), 0);
  if (bytes > 0) {
    buffer.resize(static_cast<std::size_t>(bytes));
    result.status = TransportReadStatus::Data;
    result.data = std::move(buffer);
    return result;
  }
  if (bytes == 0) {
    Close();
    result.status = TransportReadStatus::Closed;
    return result;
  }
  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
    result.status = TransportReadStatus::WouldBlock;
    return result;
  }

  result.status = TransportReadStatus::Error;
  result.error = LastSocketError("recv");
  Close();
  return result;
}

bool PosixTcpTransport::Write(const uint8_t* data, std::size_t size, std::string* error) {
  if (fd_ < 0) {
    if (error != nullptr) {
      *error = "transport is closed";
    }
    return false;
  }

  std::size_t sent = 0;
  while (sent < size) {
    const ssize_t bytes = send(fd_, data + sent, size - sent, MSG_NOSIGNAL);
    if (bytes > 0) {
      sent += static_cast<std::size_t>(bytes);
      continue;
    }
    if (bytes < 0 && errno == EINTR) {
      continue;
    }
    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (!WaitForSocketWritable(fd_, 5000, error)) {
        return false;
      }
      continue;
    }
    if (error != nullptr) {
      *error = LastSocketError("send");
    }
    Close();
    return false;
  }
  return true;
}

}  // namespace teraterm::ohos

#else
#error "PosixTcpTransport requires a POSIX socket platform."
#endif
