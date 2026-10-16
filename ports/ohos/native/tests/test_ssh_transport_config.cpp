#include "terminal_session.h"
#include "ssh_transport.h"

#include <cassert>
#include <string>

using teraterm::ohos::ConnectionMode;
using teraterm::ohos::KnownHostPolicy;
using teraterm::ohos::SshConnectionOptions;
using teraterm::ohos::SshConnectionResult;
using teraterm::ohos::SshFileTransferOptions;
using teraterm::ohos::SshPortForwardOptions;
using teraterm::ohos::SshPortForwardStartResult;
using teraterm::ohos::TerminalSession;

int main() {
  TerminalSession session(24, 80);
  SshConnectionOptions options;
  options.host = "127.0.0.1";
  options.port = 22;
  options.username = "user";
  options.password = "password";
  options.known_host_policy = KnownHostPolicy::Strict;

  SshConnectionResult result;
  const bool connected = session.ConnectSsh(options, &result);
#if TT_OHOS_ENABLE_LIBSSH2
  assert(!connected);
  assert(!result.error.empty());
#else
  assert(!connected);
  assert(result.error.find("TT_OHOS_ENABLE_LIBSSH2=ON") != std::string::npos);
#endif
  assert(session.Mode() == ConnectionMode::Local);
#if !TT_OHOS_ENABLE_LIBSSH2
  SshFileTransferOptions transfer_options;
  transfer_options.connection = options;
  transfer_options.local_path = "/tmp/local.txt";
  transfer_options.remote_path = "/tmp/remote.txt";
  assert(!teraterm::ohos::DownloadScpFile(transfer_options).success);
  assert(!teraterm::ohos::UploadScpFile(transfer_options).success);
  assert(!teraterm::ohos::DownloadSftpFile(transfer_options).success);
  assert(!teraterm::ohos::UploadSftpFile(transfer_options).success);
  SshPortForwardOptions forward_options;
  forward_options.connection = options;
  forward_options.local_host = "127.0.0.1";
  forward_options.local_port = 0;
  forward_options.remote_host = "127.0.0.1";
  forward_options.remote_port = 80;
  SshPortForwardStartResult forward_result;
  assert(teraterm::ohos::StartSshLocalPortForward(forward_options, &forward_result) == nullptr);
  assert(!forward_result.started);
  assert(forward_result.error.find("TT_OHOS_ENABLE_LIBSSH2=ON") != std::string::npos);
#endif
  return 0;
}
