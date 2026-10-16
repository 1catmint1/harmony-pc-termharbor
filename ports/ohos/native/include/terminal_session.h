#pragma once

#include "telnet_filter.h"
#include "terminal_session_core.h"
#include "ssh_transport.h"
#include "session_transport.h"

#include <cstddef>
#include <memory>
#include <string>

namespace teraterm::ohos {

class SessionTransport;

enum class ConnectionMode {
  Local,
  Tcp,
  Telnet,
  Ssh,
  Serial,
};

struct SessionPumpResult {
  std::size_t bytes_read = 0;
  bool connected = false;
  std::string error;
};

class TerminalSession {
public:
  TerminalSession(std::size_t rows, std::size_t cols);
  ~TerminalSession();

  void Resize(std::size_t rows, std::size_t cols);
  void WriteLocalOutput(const std::string& bytes);
  bool ConnectTcp(const std::string& host, uint16_t port, int timeout_ms, std::string* error);
  bool ConnectTcp(const std::string& host,
                  uint16_t port,
                  int timeout_ms,
                  const ProxyOpenOptions& proxy,
                  std::string* error);
  bool ConnectTelnet(const std::string& host, uint16_t port, int timeout_ms, std::string* error);
  bool ConnectTelnet(const std::string& host,
                     uint16_t port,
                     int timeout_ms,
                     const ProxyOpenOptions& proxy,
                     std::string* error);
  bool ConnectSerial(const SerialOpenOptions& options, std::string* error);
  bool ConnectSsh(const SshConnectionOptions& options, SshConnectionResult* result);
  void Disconnect();
  bool SendInput(const std::string& bytes, std::string* error);
  SessionPumpResult Pump(std::size_t max_bytes);
  std::string DrainTranscript();
  const std::string& OutputHistory() const { return output_history_; }

  TerminalSnapshot Snapshot() const;
  bool IsConnected() const;
  ConnectionMode Mode() const { return mode_; }

private:
  bool Connect(ConnectionMode mode,
               const std::string& host,
               uint16_t port,
               int timeout_ms,
               const ProxyOpenOptions& proxy,
               std::string* error);
  void AppendOutput(const std::string& bytes);

  TerminalSessionCore core_;
  std::string transcript_;
  std::string output_history_;
  std::unique_ptr<SessionTransport> transport_;
  TelnetFilter telnet_;
  ConnectionMode mode_ = ConnectionMode::Local;
};

}  // namespace teraterm::ohos
