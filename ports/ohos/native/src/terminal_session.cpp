#include "terminal_session.h"

#include "posix_serial_transport.h"
#include "posix_tcp_transport.h"
#include "session_transport.h"
#include "ssh_transport.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace teraterm::ohos {
namespace {

std::vector<uint8_t> ToBytes(const std::string& text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}

std::string ToString(const std::vector<uint8_t>& bytes) {
  return std::string(bytes.begin(), bytes.end());
}

constexpr std::size_t kMaxOutputHistory = 1024 * 1024;

}  // namespace

TerminalSession::TerminalSession(std::size_t rows, std::size_t cols)
    : core_(rows, cols) {}

TerminalSession::~TerminalSession() {
  Disconnect();
}

void TerminalSession::Resize(std::size_t rows, std::size_t cols) {
  core_.Resize(rows, cols);
  if (mode_ == ConnectionMode::Telnet && IsConnected()) {
    const std::vector<uint8_t> command = telnet_.BuildResizeCommand(rows, cols);
    std::string ignored_error;
    transport_->Write(command.data(), command.size(), &ignored_error);
  } else if (mode_ == ConnectionMode::Ssh && IsConnected()) {
    auto* ssh_transport = dynamic_cast<SshSessionTransport*>(transport_.get());
    if (ssh_transport != nullptr) {
      ssh_transport->ResizePty(rows, cols);
    }
  }
}

void TerminalSession::WriteLocalOutput(const std::string& bytes) {
  AppendOutput(bytes);
}

bool TerminalSession::ConnectTcp(const std::string& host,
                                 uint16_t port,
                                 int timeout_ms,
                                 std::string* error) {
  return Connect(ConnectionMode::Tcp, host, port, timeout_ms, ProxyOpenOptions{}, error);
}

bool TerminalSession::ConnectTcp(const std::string& host,
                                 uint16_t port,
                                 int timeout_ms,
                                 const ProxyOpenOptions& proxy,
                                 std::string* error) {
  return Connect(ConnectionMode::Tcp, host, port, timeout_ms, proxy, error);
}

bool TerminalSession::ConnectTelnet(const std::string& host,
                                    uint16_t port,
                                    int timeout_ms,
                                    std::string* error) {
  return Connect(ConnectionMode::Telnet, host, port == 0 ? 23 : port, timeout_ms, ProxyOpenOptions{}, error);
}

bool TerminalSession::ConnectTelnet(const std::string& host,
                                    uint16_t port,
                                    int timeout_ms,
                                    const ProxyOpenOptions& proxy,
                                    std::string* error) {
  return Connect(ConnectionMode::Telnet, host, port == 0 ? 23 : port, timeout_ms, proxy, error);
}

bool TerminalSession::ConnectSerial(const SerialOpenOptions& options, std::string* error) {
  Disconnect();
  auto transport = std::make_unique<PosixSerialTransport>();
  SerialOpenOptions effective_options = options;
  effective_options.timeout_ms = effective_options.timeout_ms <= 0 ? 5000 : effective_options.timeout_ms;
  if (!transport->ConnectSerial(effective_options, error)) {
    return false;
  }
  mode_ = ConnectionMode::Serial;
  transport_ = std::move(transport);
  return true;
}

bool TerminalSession::ConnectSsh(const SshConnectionOptions& options, SshConnectionResult* result) {
  Disconnect();
  auto transport = CreateSshSessionTransport();
  SshConnectionOptions effective_options = options;
  effective_options.rows = core_.Rows();
  effective_options.cols = core_.Cols();
  if (!transport->ConnectSsh(effective_options, result)) {
    return false;
  }
  mode_ = ConnectionMode::Ssh;
  transport_ = std::move(transport);
  return true;
}

void TerminalSession::Disconnect() {
  if (transport_) {
    transport_->Close();
  }
  transport_.reset();
  mode_ = ConnectionMode::Local;
}

bool TerminalSession::SendInput(const std::string& bytes, std::string* error) {
  if (!IsConnected()) {
    if (error != nullptr) {
      *error = "session is not connected";
    }
    return false;
  }

  std::vector<uint8_t> output = ToBytes(bytes);
  if (mode_ == ConnectionMode::Telnet) {
    output = telnet_.ProcessOutbound(output);
  }
  return transport_->Write(output.data(), output.size(), error);
}

SessionPumpResult TerminalSession::Pump(std::size_t max_bytes) {
  SessionPumpResult result;
  result.connected = IsConnected();
  if (!transport_) {
    return result;
  }

  std::size_t remaining = max_bytes == 0 ? 65536 : max_bytes;
  while (remaining > 0) {
    TransportReadResult read = transport_->Read(std::min<std::size_t>(remaining, 4096));
    if (read.status == TransportReadStatus::WouldBlock) {
      result.connected = IsConnected();
      return result;
    }
    if (read.status == TransportReadStatus::Closed) {
      result.connected = false;
      return result;
    }
    if (read.status == TransportReadStatus::Error) {
      result.connected = false;
      result.error = std::move(read.error);
      return result;
    }

    result.bytes_read += read.data.size();
    remaining -= read.data.size();

    if (mode_ == ConnectionMode::Telnet) {
      std::vector<uint8_t> terminal_output;
      std::vector<uint8_t> reply;
      telnet_.ProcessInbound(read.data, &terminal_output, &reply);
      if (!reply.empty()) {
        std::string write_error;
        if (!transport_->Write(reply.data(), reply.size(), &write_error)) {
          result.connected = false;
          result.error = write_error;
          return result;
        }
      }
      if (!terminal_output.empty()) {
        const std::string text = ToString(terminal_output);
        AppendOutput(text);
      }
    } else {
      const std::string text = ToString(read.data);
      AppendOutput(text);
    }
  }

  result.connected = IsConnected();
  return result;
}

std::string TerminalSession::DrainTranscript() {
  std::string result = std::move(transcript_);
  transcript_.clear();
  return result;
}

TerminalSnapshot TerminalSession::Snapshot() const {
  return core_.Snapshot();
}

bool TerminalSession::IsConnected() const {
  return transport_ && transport_->IsOpen();
}

bool TerminalSession::Connect(ConnectionMode mode,
                              const std::string& host,
                              uint16_t port,
                              int timeout_ms,
                              const ProxyOpenOptions& proxy,
                              std::string* error) {
  Disconnect();
  auto transport = std::make_unique<PosixTcpTransport>();
  TransportOpenOptions options;
  options.host = host;
  options.port = port;
  options.timeout_ms = timeout_ms <= 0 ? 5000 : timeout_ms;
  options.proxy = proxy;
  if (!transport->Connect(options, error)) {
    return false;
  }
  mode_ = mode;
  transport_ = std::move(transport);
  return true;
}

void TerminalSession::AppendOutput(const std::string& bytes) {
  core_.WriteUtf8(bytes);
  transcript_ += bytes;
  output_history_ += bytes;
  if (output_history_.size() > kMaxOutputHistory) {
    output_history_.erase(0, output_history_.size() - kMaxOutputHistory);
  }
}

}  // namespace teraterm::ohos
