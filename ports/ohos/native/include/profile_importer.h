#pragma once

#include "session_transport.h"

#include <cstdint>
#include <string>
#include <vector>

namespace teraterm::ohos {

enum class ProfileProtocol {
  Tcp,
  Telnet,
  Ssh,
  Serial,
  Unsupported,
};

struct ConnectionProfile {
  std::string name;
  ProfileProtocol protocol = ProfileProtocol::Telnet;
  std::string host;
  uint16_t port = 23;
  std::string username;
  std::string password;
  std::string public_key_path;
  std::string private_key_path;
  std::string passphrase;
  ProxyOpenOptions proxy;
  SerialOpenOptions serial;
  std::string setup_file;
  std::string macro_file;
  std::string source;
  std::vector<std::string> warnings;
};

class TeraTermIniImporter {
public:
  static std::vector<ConnectionProfile> ImportHostProfiles(const std::string& ini_text);
};

std::string ProfileProtocolName(ProfileProtocol protocol);

}  // namespace teraterm::ohos
