#include "profile_importer.h"

#include <cassert>
#include <string>

using teraterm::ohos::ProfileProtocol;
using teraterm::ohos::ProxyType;
using teraterm::ohos::TeraTermIniImporter;

int main() {
  const std::string ini =
      "[Tera Term]\n"
      "Telnet=on\n"
      "[Hosts]\n"
      "Host1=myhost.example.com\n"
      "Host2=user@myhost.example.com:10022 /ssh /F=myhost.ini /M=login.ttl /passwd=secret /keyfile=id_ed25519\n"
      "Host3=ssh://admin@example.net/\n"
      "Host4=/C=2 /BAUD=115200 /CDATABIT=7 /CPARITY=even /CSTOPBIT=2 /CFLOWCTRL=x ;serial port\n"
      "Host5=\\\\.\\pipe\\vmware-serial-port ;Named pipe\n"
      "Host6=target.example.com:22 /ssh -proxy=http://proxy_user:proxy_pass@proxy.example.com:8080\n"
      "Host7=target.example.net:23 /nossh -proxy=socks5://socks_user:socks_pass@socks.example.com:1080\n"
      "Host8=legacy.example.net:23 /nossh -proxy=socks4://socks_id@socks4.example.com:1080\n"
      "Host9=jump.example.net:23 /nossh -proxy=telnet://jump_user:jump_pass@telnet-proxy.example.com\n";

  const auto profiles = TeraTermIniImporter::ImportHostProfiles(ini);
  assert(profiles.size() == 9);

  assert(profiles[0].name == "host1");
  assert(profiles[0].protocol == ProfileProtocol::Telnet);
  assert(profiles[0].host == "myhost.example.com");
  assert(profiles[0].port == 23);

  assert(profiles[1].protocol == ProfileProtocol::Ssh);
  assert(profiles[1].host == "myhost.example.com");
  assert(profiles[1].username == "user");
  assert(profiles[1].port == 10022);
  assert(profiles[1].password == "secret");
  assert(profiles[1].private_key_path == "id_ed25519");
  assert(profiles[1].setup_file == "myhost.ini");
  assert(profiles[1].macro_file == "login.ttl");

  assert(profiles[2].protocol == ProfileProtocol::Ssh);
  assert(profiles[2].host == "example.net");
  assert(profiles[2].username == "admin");
  assert(profiles[2].port == 22);

  assert(profiles[3].protocol == ProfileProtocol::Serial);
  assert(profiles[3].host == "/dev/ttyS1");
  assert(profiles[3].serial.device_path == "/dev/ttyS1");
  assert(profiles[3].serial.baud_rate == 115200);
  assert(profiles[3].serial.data_bits == 7);
  assert(profiles[3].serial.stop_bits == 2);
  assert(profiles[3].serial.parity == teraterm::ohos::SerialParity::Even);
  assert(profiles[3].serial.flow_control == teraterm::ohos::SerialFlowControl::Software);
  assert(!profiles[3].warnings.empty());

  assert(profiles[4].protocol == ProfileProtocol::Unsupported);
  assert(!profiles[4].warnings.empty());

  assert(profiles[5].protocol == ProfileProtocol::Ssh);
  assert(profiles[5].host == "target.example.com");
  assert(profiles[5].proxy.type == ProxyType::HttpConnect);
  assert(profiles[5].proxy.host == "proxy.example.com");
  assert(profiles[5].proxy.port == 8080);
  assert(profiles[5].proxy.username == "proxy_user");
  assert(profiles[5].proxy.password == "proxy_pass");

  assert(profiles[6].protocol == ProfileProtocol::Telnet);
  assert(profiles[6].host == "target.example.net");
  assert(profiles[6].proxy.type == ProxyType::Socks5);
  assert(profiles[6].proxy.host == "socks.example.com");
  assert(profiles[6].proxy.port == 1080);
  assert(profiles[6].proxy.username == "socks_user");
  assert(profiles[6].proxy.password == "socks_pass");

  assert(profiles[7].protocol == ProfileProtocol::Telnet);
  assert(profiles[7].host == "legacy.example.net");
  assert(profiles[7].proxy.type == ProxyType::Socks4);
  assert(profiles[7].proxy.host == "socks4.example.com");
  assert(profiles[7].proxy.port == 1080);
  assert(profiles[7].proxy.username == "socks_id");

  assert(profiles[8].protocol == ProfileProtocol::Telnet);
  assert(profiles[8].host == "jump.example.net");
  assert(profiles[8].proxy.type == ProxyType::Telnet);
  assert(profiles[8].proxy.host == "telnet-proxy.example.com");
  assert(profiles[8].proxy.port == 23);
  assert(profiles[8].proxy.username == "jump_user");
  assert(profiles[8].proxy.password == "jump_pass");
  return 0;
}
