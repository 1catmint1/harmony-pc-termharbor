#include "posix_serial_transport.h"

#ifndef _WIN32

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace teraterm::ohos {
namespace {

std::string LastSerialError(const char* action) {
  std::ostringstream stream;
  stream << action << ": " << std::strerror(errno);
  return stream.str();
}

bool SetNonBlocking(int fd, std::string* error) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    if (error != nullptr) {
      *error = LastSerialError("fcntl(F_GETFL)");
    }
    return false;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    if (error != nullptr) {
      *error = LastSerialError("fcntl(F_SETFL)");
    }
    return false;
  }
  return true;
}

bool WaitForFdWritable(int fd, int timeout_ms, std::string* error) {
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
    *error = result == 0 ? "serial write timeout" : LastSerialError("select");
  }
  return false;
}

bool BaudConstant(uint32_t baud_rate, speed_t* baud) {
  switch (baud_rate) {
    case 110:
      *baud = B110;
      return true;
    case 300:
      *baud = B300;
      return true;
    case 600:
      *baud = B600;
      return true;
    case 1200:
      *baud = B1200;
      return true;
    case 2400:
      *baud = B2400;
      return true;
    case 4800:
      *baud = B4800;
      return true;
    case 9600:
      *baud = B9600;
      return true;
    case 19200:
      *baud = B19200;
      return true;
    case 38400:
      *baud = B38400;
      return true;
#ifdef B57600
    case 57600:
      *baud = B57600;
      return true;
#endif
#ifdef B115200
    case 115200:
      *baud = B115200;
      return true;
#endif
#ifdef B230400
    case 230400:
      *baud = B230400;
      return true;
#endif
#ifdef B460800
    case 460800:
      *baud = B460800;
      return true;
#endif
#ifdef B921600
    case 921600:
      *baud = B921600;
      return true;
#endif
    default:
      return false;
  }
}

bool ConfigurePort(int fd, const SerialOpenOptions& options, std::string* error) {
  termios tio = {};
  if (tcgetattr(fd, &tio) < 0) {
    if (error != nullptr) {
      *error = LastSerialError("tcgetattr");
    }
    return false;
  }

  speed_t baud = B9600;
  if (!BaudConstant(options.baud_rate == 0 ? 9600 : options.baud_rate, &baud)) {
    if (error != nullptr) {
      std::ostringstream stream;
      stream << "unsupported serial baud rate: " << options.baud_rate;
      *error = stream.str();
    }
    return false;
  }

  cfsetispeed(&tio, baud);
  cfsetospeed(&tio, baud);

  tio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY);
  tio.c_oflag &= ~OPOST;
  tio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  tio.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
  tio.c_cflag |= CLOCAL | CREAD;

  if (options.data_bits == 7) {
    tio.c_cflag |= CS7;
  } else if (options.data_bits == 8 || options.data_bits == 0) {
    tio.c_cflag |= CS8;
  } else {
    if (error != nullptr) {
      *error = "serial data bits must be 7 or 8";
    }
    return false;
  }

  if (options.stop_bits == 2) {
    tio.c_cflag |= CSTOPB;
  } else if (options.stop_bits != 1 && options.stop_bits != 0) {
    if (error != nullptr) {
      *error = "serial stop bits must be 1 or 2";
    }
    return false;
  }

  if (options.parity == SerialParity::Odd) {
    tio.c_cflag |= PARENB | PARODD;
  } else if (options.parity == SerialParity::Even) {
    tio.c_cflag |= PARENB;
  }

  if (options.flow_control == SerialFlowControl::Software) {
    tio.c_iflag |= IXON | IXOFF;
  } else if (options.flow_control == SerialFlowControl::Hardware) {
#if defined(CRTSCTS)
    tio.c_cflag |= CRTSCTS;
#elif defined(CCTS_OFLOW) && defined(CRTS_IFLOW)
    tio.c_cflag |= CCTS_OFLOW | CRTS_IFLOW;
#else
    if (error != nullptr) {
      *error = "hardware serial flow control is not available on this platform";
    }
    return false;
#endif
  }

  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;

  if (tcsetattr(fd, TCSANOW, &tio) < 0) {
    if (error != nullptr) {
      *error = LastSerialError("tcsetattr");
    }
    return false;
  }
  tcflush(fd, TCIOFLUSH);
  return true;
}

}  // namespace

PosixSerialTransport::PosixSerialTransport() = default;

PosixSerialTransport::~PosixSerialTransport() {
  Close();
}

bool PosixSerialTransport::Connect(const TransportOpenOptions& options, std::string* error) {
  SerialOpenOptions serial_options;
  serial_options.device_path = options.host;
  serial_options.baud_rate = options.port == 0 ? 9600 : options.port;
  serial_options.timeout_ms = options.timeout_ms;
  return ConnectSerial(serial_options, error);
}

bool PosixSerialTransport::ConnectSerial(const SerialOpenOptions& options, std::string* error) {
  Close();
  if (options.device_path.empty()) {
    if (error != nullptr) {
      *error = "serial device path is required";
    }
    return false;
  }

  fd_ = open(options.device_path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd_ < 0) {
    if (error != nullptr) {
      *error = LastSerialError("open");
    }
    return false;
  }

  if (!ConfigurePort(fd_, options, error) || !SetNonBlocking(fd_, error)) {
    Close();
    return false;
  }
  tcdrain(fd_);
  return true;
}

void PosixSerialTransport::Close() {
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

bool PosixSerialTransport::IsOpen() const {
  return fd_ >= 0;
}

TransportReadResult PosixSerialTransport::Read(std::size_t max_bytes) {
  TransportReadResult result;
  if (fd_ < 0) {
    result.status = TransportReadStatus::Closed;
    return result;
  }

  std::vector<uint8_t> buffer(max_bytes == 0 ? 4096 : max_bytes);
  const ssize_t bytes = read(fd_, buffer.data(), buffer.size());
  if (bytes > 0) {
    buffer.resize(static_cast<std::size_t>(bytes));
    result.status = TransportReadStatus::Data;
    result.data = std::move(buffer);
    return result;
  }
  if (bytes == 0 || errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
    result.status = TransportReadStatus::WouldBlock;
    return result;
  }

  result.status = TransportReadStatus::Error;
  result.error = LastSerialError("read");
  Close();
  return result;
}

bool PosixSerialTransport::Write(const uint8_t* data, std::size_t size, std::string* error) {
  if (fd_ < 0) {
    if (error != nullptr) {
      *error = "transport is closed";
    }
    return false;
  }

  std::size_t sent = 0;
  while (sent < size) {
    const ssize_t bytes = write(fd_, data + sent, size - sent);
    if (bytes > 0) {
      sent += static_cast<std::size_t>(bytes);
      continue;
    }
    if (bytes < 0 && errno == EINTR) {
      continue;
    }
    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (!WaitForFdWritable(fd_, 5000, error)) {
        return false;
      }
      continue;
    }
    if (error != nullptr) {
      *error = LastSerialError("write");
    }
    Close();
    return false;
  }
  return true;
}

}  // namespace teraterm::ohos

#else
#error "PosixSerialTransport requires a POSIX termios platform."
#endif
