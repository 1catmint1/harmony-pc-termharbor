#include "ssh_transport.h"

namespace teraterm::ohos {
namespace {

class UnavailableSshSessionTransport final : public SshSessionTransport {
public:
  bool Connect(const TransportOpenOptions&, std::string* error) override {
    if (error != nullptr) {
      *error = "SSH transport uses ConnectSsh";
    }
    return false;
  }

  bool ConnectSsh(const SshConnectionOptions&, SshConnectionResult* result) override {
    if (result != nullptr) {
      result->connected = false;
      result->error = "SSH transport is not enabled. Build with TT_OHOS_ENABLE_LIBSSH2=ON and libssh2.";
    }
    return false;
  }

  void Close() override {}
  bool IsOpen() const override { return false; }

  TransportReadResult Read(std::size_t) override {
    TransportReadResult result;
    result.status = TransportReadStatus::Closed;
    return result;
  }

  bool Write(const uint8_t*, std::size_t, std::string* error) override {
    if (error != nullptr) {
      *error = "SSH transport is not enabled";
    }
    return false;
  }

  void ResizePty(std::size_t, std::size_t) override {}
};

}  // namespace

std::unique_ptr<SshSessionTransport> CreateSshSessionTransport() {
  return std::make_unique<UnavailableSshSessionTransport>();
}

SshFileTransferResult DownloadScpFile(const SshFileTransferOptions&) {
  SshFileTransferResult result;
  result.error = "SSH file transfer is not enabled. Build with TT_OHOS_ENABLE_LIBSSH2=ON and libssh2.";
  return result;
}

SshFileTransferResult UploadScpFile(const SshFileTransferOptions&) {
  SshFileTransferResult result;
  result.error = "SSH file transfer is not enabled. Build with TT_OHOS_ENABLE_LIBSSH2=ON and libssh2.";
  return result;
}

SshFileTransferResult DownloadSftpFile(const SshFileTransferOptions&) {
  SshFileTransferResult result;
  result.error = "SSH file transfer is not enabled. Build with TT_OHOS_ENABLE_LIBSSH2=ON and libssh2.";
  return result;
}

SshFileTransferResult UploadSftpFile(const SshFileTransferOptions&) {
  SshFileTransferResult result;
  result.error = "SSH file transfer is not enabled. Build with TT_OHOS_ENABLE_LIBSSH2=ON and libssh2.";
  return result;
}

std::unique_ptr<SshPortForwardHandle> StartSshLocalPortForward(const SshPortForwardOptions&,
                                                               SshPortForwardStartResult* result) {
  if (result != nullptr) {
    result->started = false;
    result->error = "SSH port forwarding is not enabled. Build with TT_OHOS_ENABLE_LIBSSH2=ON and libssh2.";
  }
  return nullptr;
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
