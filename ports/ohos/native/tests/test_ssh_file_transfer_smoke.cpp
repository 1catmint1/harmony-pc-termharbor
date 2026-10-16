#include "ssh_transport.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::string EnvOrDefault(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  return value == nullptr || value[0] == '\0' ? fallback : value;
}

uint16_t PortOrDefault(const char* name, uint16_t fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  const int parsed = std::atoi(value);
  return parsed > 0 && parsed <= 65535 ? static_cast<uint16_t>(parsed) : fallback;
}

void WriteTextFile(const std::string& path, const std::string& text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  assert(output.is_open());
  output << text;
  assert(output.good());
}

std::string ReadTextFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.is_open());
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
#if TT_OHOS_ENABLE_LIBSSH2
  teraterm::ohos::SshConnectionOptions connection;
  connection.host = EnvOrDefault("TT_OHOS_SSH_SMOKE_HOST", "");
  if (connection.host.empty()) {
    return 0;
  }
  connection.port = PortOrDefault("TT_OHOS_SSH_SMOKE_PORT", 22);
  connection.username = EnvOrDefault("TT_OHOS_SSH_SMOKE_USER", "ttuser");
  connection.password = EnvOrDefault("TT_OHOS_SSH_SMOKE_PASSWORD", "ttpass");
  connection.known_host_policy = teraterm::ohos::KnownHostPolicy::AcceptNew;
  connection.timeout_ms = 10000;

  const std::string local_download = EnvOrDefault(
      "TT_OHOS_SSH_SMOKE_LOCAL_DOWNLOAD", "/data/local/tmp/teraterm_scp_download.txt");
  const std::string local_upload = EnvOrDefault(
      "TT_OHOS_SSH_SMOKE_LOCAL_UPLOAD", "/data/local/tmp/teraterm_scp_upload.txt");
  const std::string remote_download = EnvOrDefault("TT_OHOS_SSH_SMOKE_REMOTE_DOWNLOAD", "/download.txt");
  const std::string remote_upload = EnvOrDefault("TT_OHOS_SSH_SMOKE_REMOTE_UPLOAD", "/upload-scp.txt");

  teraterm::ohos::SshFileTransferOptions download_options;
  download_options.connection = connection;
  download_options.local_path = local_download;
  download_options.remote_path = remote_download;

  const teraterm::ohos::SshFileTransferResult scp_download =
      teraterm::ohos::DownloadScpFile(download_options);
  if (!scp_download.success) {
    std::cerr << "SCP download failed: " << scp_download.error << "\n";
    return 2;
  }
  if (ReadTextFile(local_download).empty()) {
    std::cerr << "SCP download produced an empty file\n";
    return 3;
  }

  WriteTextFile(local_upload, "upload from teraterm ohos scp smoke\n");
  teraterm::ohos::SshFileTransferOptions upload_options;
  upload_options.connection = connection;
  upload_options.local_path = local_upload;
  upload_options.remote_path = remote_upload;

  const teraterm::ohos::SshFileTransferResult scp_upload =
      teraterm::ohos::UploadScpFile(upload_options);
  if (!scp_upload.success) {
    std::cerr << "SCP upload failed: " << scp_upload.error << "\n";
    return 4;
  }
  std::cout << "SCP smoke passed: " << scp_download.bytes_transferred << " down, "
            << scp_upload.bytes_transferred << " up\n";
#endif
  return 0;
}
