#pragma once

#include "known_hosts_store.h"
#include "session_transport.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace teraterm::ohos {

enum class KnownHostPolicy {
  Strict,
  AcceptNew,
  AcceptChanged,
};

struct SshConnectionOptions {
  std::string host;
  uint16_t port = 22;
  std::string username;
  std::string password;
  std::string public_key_path;
  std::string private_key_path;
  std::string passphrase;
  std::string known_hosts_text;
  KnownHostPolicy known_host_policy = KnownHostPolicy::Strict;
  std::string terminal_type = "xterm-256color";
  ProxyOpenOptions proxy;
  std::size_t rows = 24;
  std::size_t cols = 80;
  int timeout_ms = 5000;
};

struct SshConnectionResult {
  bool connected = false;
  std::string error;
  std::string key_type;
  std::string key_base64;
  std::string updated_known_hosts_text;
  KnownHostMatch host_key_match;
};

struct SshFileTransferOptions {
  SshConnectionOptions connection;
  std::string local_path;
  std::string remote_path;
  int mode = 0644;
};

struct SshFileTransferResult {
  bool success = false;
  std::string error;
  std::size_t bytes_transferred = 0;
  std::string updated_known_hosts_text;
  KnownHostMatch host_key_match;
};

struct SshPortForwardOptions {
  SshConnectionOptions connection;
  std::string local_host = "127.0.0.1";
  uint16_t local_port = 0;
  std::string remote_host;
  uint16_t remote_port = 0;
};

struct SshPortForwardStartResult {
  bool started = false;
  std::string error;
  uint16_t bound_port = 0;
  std::string updated_known_hosts_text;
  KnownHostMatch host_key_match;
};

class SshSessionTransport : public SessionTransport {
public:
  ~SshSessionTransport() override = default;
  virtual bool ConnectSsh(const SshConnectionOptions& options, SshConnectionResult* result) = 0;
  virtual void ResizePty(std::size_t rows, std::size_t cols) = 0;
};

class SshPortForwardHandle {
public:
  virtual ~SshPortForwardHandle() = default;
  virtual void Stop() = 0;
  virtual uint16_t BoundPort() const = 0;
};

std::unique_ptr<SshSessionTransport> CreateSshSessionTransport();
SshFileTransferResult DownloadScpFile(const SshFileTransferOptions& options);
SshFileTransferResult UploadScpFile(const SshFileTransferOptions& options);
SshFileTransferResult DownloadSftpFile(const SshFileTransferOptions& options);
SshFileTransferResult UploadSftpFile(const SshFileTransferOptions& options);
std::unique_ptr<SshPortForwardHandle> StartSshLocalPortForward(const SshPortForwardOptions& options,
                                                               SshPortForwardStartResult* result);
KnownHostPolicy KnownHostPolicyFromString(const std::string& value);
std::string KnownHostPolicyName(KnownHostPolicy policy);

}  // namespace teraterm::ohos
