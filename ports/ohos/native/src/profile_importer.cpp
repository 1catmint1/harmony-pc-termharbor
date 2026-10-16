#include "profile_importer.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>

namespace teraterm::ohos {
namespace {

struct IniData {
  std::map<std::string, std::map<std::string, std::string>> sections;
};

std::string Trim(const std::string& value) {
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return value.substr(begin, end - begin);
}

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::string StripInlineComment(const std::string& line) {
  bool in_quote = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '"') {
      in_quote = !in_quote;
    } else if (line[i] == ';' && !in_quote) {
      return line.substr(0, i);
    }
  }
  return line;
}

std::string Unquote(std::string value) {
  value = Trim(value);
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

IniData ParseIni(const std::string& ini_text) {
  IniData data;
  std::string section;
  std::stringstream stream(ini_text);
  std::string line;

  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    line = Trim(StripInlineComment(line));
    if (line.empty()) {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      section = Lower(Trim(line.substr(1, line.size() - 2)));
      continue;
    }

    const std::size_t equals = line.find('=');
    if (equals == std::string::npos) {
      continue;
    }
    const std::string key = Lower(Trim(line.substr(0, equals)));
    const std::string value = Unquote(line.substr(equals + 1));
    data.sections[section][key] = value;
  }
  return data;
}

std::string GetValue(const IniData& data,
                     const std::string& section,
                     const std::string& key,
                     const std::string& fallback = "") {
  const auto section_iter = data.sections.find(Lower(section));
  if (section_iter == data.sections.end()) {
    return fallback;
  }
  const auto key_iter = section_iter->second.find(Lower(key));
  if (key_iter == section_iter->second.end()) {
    return fallback;
  }
  return key_iter->second;
}

std::vector<std::string> TokenizeCommandLine(const std::string& value) {
  std::vector<std::string> tokens;
  std::string token;
  bool in_quote = false;
  for (char ch : value) {
    if (ch == '"') {
      in_quote = !in_quote;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(ch)) != 0 && !in_quote) {
      if (!token.empty()) {
        tokens.push_back(token);
        token.clear();
      }
      continue;
    }
    token.push_back(ch);
  }
  if (!token.empty()) {
    tokens.push_back(token);
  }
  return tokens;
}

bool StartsWithInsensitive(const std::string& value, const std::string& prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }
  return Lower(value.substr(0, prefix.size())) == Lower(prefix);
}

uint16_t ParsePort(const std::string& value, uint16_t fallback) {
  char* end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || parsed <= 0 || parsed > 65535) {
    return fallback;
  }
  return static_cast<uint16_t>(parsed);
}

uint32_t ParseUnsigned(const std::string& value, uint32_t fallback, uint32_t minimum, uint32_t maximum) {
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
  if (end == value.c_str() || parsed < minimum || parsed > maximum) {
    return fallback;
  }
  return static_cast<uint32_t>(parsed);
}

uint8_t ParseDataBits(const std::string& value, uint8_t fallback) {
  const uint32_t parsed = ParseUnsigned(value, fallback, 7, 8);
  return static_cast<uint8_t>(parsed);
}

uint8_t ParseStopBits(const std::string& value, uint8_t fallback) {
  const uint32_t parsed = ParseUnsigned(value, fallback, 1, 2);
  return static_cast<uint8_t>(parsed);
}

std::string SerialDevicePathFromComSpec(const std::string& value) {
  const std::string trimmed = Trim(Unquote(value));
  if (trimmed.empty()) {
    return "";
  }
  const std::string lower = Lower(trimmed);
  if (StartsWithInsensitive(lower, "/dev/") || lower.find('/') != std::string::npos) {
    return trimmed;
  }

  std::string number_text = trimmed;
  if (StartsWithInsensitive(lower, "com")) {
    number_text = trimmed.substr(3);
  }
  char* end = nullptr;
  const long number = std::strtol(number_text.c_str(), &end, 10);
  if (end != number_text.c_str() && *end == '\0' && number > 0) {
    return "/dev/ttyS" + std::to_string(number - 1);
  }
  return trimmed;
}

SerialOpenOptions ReadSerialDefaults(const IniData& data) {
  SerialOpenOptions options;
  options.device_path = SerialDevicePathFromComSpec(GetValue(data, "Tera Term", "ComPort", "1"));
  options.baud_rate = ParseUnsigned(GetValue(data, "Tera Term", "BaudRate", "9600"), 9600, 1, 4000000);
  options.data_bits = ParseDataBits(GetValue(data, "Tera Term", "DataBit", "8"), 8);
  options.stop_bits = ParseStopBits(GetValue(data, "Tera Term", "StopBit", "1"), 1);
  options.parity = SerialParityFromString(GetValue(data, "Tera Term", "Parity", "none"));
  options.flow_control = SerialFlowControlFromString(GetValue(data, "Tera Term", "FlowCtrl", "none"));
  return options;
}

void ParseHostPort(const std::string& input, ConnectionProfile* profile) {
  std::string value = input;
  const std::size_t at = value.find('@');
  if (at != std::string::npos) {
    profile->username = value.substr(0, at);
    value = value.substr(at + 1);
  }

  if (value.empty()) {
    return;
  }

  if (value.front() == '[') {
    const std::size_t close = value.find(']');
    if (close != std::string::npos) {
      profile->host = value.substr(0, close + 1);
      if (close + 2 < value.size() && value[close + 1] == ':') {
        profile->port = ParsePort(value.substr(close + 2), profile->port);
      }
      return;
    }
  }

  const std::size_t colon = value.rfind(':');
  if (colon != std::string::npos && value.find(':') == colon) {
    profile->host = value.substr(0, colon);
    profile->port = ParsePort(value.substr(colon + 1), profile->port);
  } else {
    profile->host = value;
  }
}

void ParseProxyHostPort(const std::string& input, ProxyOpenOptions* proxy) {
  std::string value = input;
  const std::size_t slash = value.find('/');
  if (slash != std::string::npos) {
    value = value.substr(0, slash);
  }

  const std::size_t at = value.rfind('@');
  if (at != std::string::npos) {
    const std::string user_info = value.substr(0, at);
    const std::size_t colon = user_info.find(':');
    if (colon == std::string::npos) {
      proxy->username = user_info;
    } else {
      proxy->username = user_info.substr(0, colon);
      proxy->password = user_info.substr(colon + 1);
    }
    value = value.substr(at + 1);
  }

  if (value.empty()) {
    return;
  }

  if (value.front() == '[') {
    const std::size_t close = value.find(']');
    if (close != std::string::npos) {
      proxy->host = value.substr(0, close + 1);
      if (close + 2 < value.size() && value[close + 1] == ':') {
        proxy->port = ParsePort(value.substr(close + 2), proxy->port);
      }
      return;
    }
  }

  const std::size_t colon = value.rfind(':');
  if (colon != std::string::npos && value.find(':') == colon) {
    proxy->host = value.substr(0, colon);
    proxy->port = ParsePort(value.substr(colon + 1), proxy->port);
  } else {
    proxy->host = value;
  }
}

void ParseProxyUrl(const std::string& token_value, ConnectionProfile* profile) {
  const std::string value = Unquote(token_value);
  if (Lower(value) == "none") {
    profile->proxy = ProxyOpenOptions{};
    return;
  }

  const std::size_t scheme = value.find("://");
  if (scheme == std::string::npos) {
    profile->warnings.push_back("proxy URL requires a protocol such as http://");
    return;
  }

  const std::string protocol = Lower(value.substr(0, scheme));
  if (protocol != "http" && protocol != "telnet" && protocol != "socks4" && protocol != "socks4a" &&
      protocol != "socks5" && protocol != "socks") {
    profile->warnings.push_back(protocol + " proxy is not supported by the HarmonyOS seed yet");
    return;
  }

  ProxyOpenOptions proxy;
  if (protocol == "http") {
    proxy.type = ProxyType::HttpConnect;
  } else if (protocol == "telnet") {
    proxy.type = ProxyType::Telnet;
  } else if (protocol == "socks4" || protocol == "socks4a") {
    proxy.type = ProxyType::Socks4;
  } else {
    proxy.type = ProxyType::Socks5;
  }
  proxy.port = protocol == "http" ? 8080 : protocol == "telnet" ? 23 : 1080;
  ParseProxyHostPort(value.substr(scheme + 3), &proxy);
  if (proxy.host.empty() || proxy.port == 0) {
    profile->warnings.push_back(protocol + " proxy host or port could not be parsed");
    return;
  }
  profile->proxy = proxy;
}

void ParseSshUrl(const std::string& token, ConnectionProfile* profile) {
  profile->protocol = ProfileProtocol::Ssh;
  profile->port = 22;
  std::string rest = token.substr(token.find("://") + 3);
  if (!rest.empty() && rest.back() == '/') {
    rest.pop_back();
  }
  ParseHostPort(rest, profile);
}

ConnectionProfile ParseHostEntry(const IniData& data, const std::string& key, const std::string& value) {
  ConnectionProfile profile;
  profile.name = key;
  profile.source = value;
  profile.serial = ReadSerialDefaults(data);

  const std::string telnet_default = Lower(GetValue(data, "Tera Term", "Telnet", "on"));
  const std::string port_type = Lower(GetValue(data, "Tera Term", "Port", "tcpip"));
  if (port_type == "serial") {
    profile.protocol = ProfileProtocol::Serial;
    profile.host = profile.serial.device_path;
    profile.port = 0;
  } else {
    profile.protocol = telnet_default == "off" ? ProfileProtocol::Tcp : ProfileProtocol::Telnet;
    profile.port = profile.protocol == ProfileProtocol::Telnet ? 23 :
                   ParsePort(GetValue(data, "Tera Term", "TCPPort"), 23);
  }

  const std::vector<std::string> tokens = TokenizeCommandLine(value);
  for (const std::string& token : tokens) {
    const std::string lower = Lower(token);
    if (StartsWithInsensitive(lower, "ssh://") ||
        StartsWithInsensitive(lower, "ssh1://") ||
        StartsWithInsensitive(lower, "ssh2://") ||
        StartsWithInsensitive(lower, "slogin://") ||
        StartsWithInsensitive(lower, "slogin1://") ||
        StartsWithInsensitive(lower, "slogin2://")) {
      ParseSshUrl(token, &profile);
    } else if (lower == "/ssh" || lower == "/1" || lower == "/2" ||
               StartsWithInsensitive(lower, "/t=2")) {
      profile.protocol = ProfileProtocol::Ssh;
      if (profile.port == 23) {
        profile.port = 22;
      }
    } else if (lower == "/telnet" || lower == "/nossh" || StartsWithInsensitive(lower, "/t=1")) {
      profile.protocol = ProfileProtocol::Telnet;
      if (profile.port == 22) {
        profile.port = 23;
      }
    } else if (StartsWithInsensitive(lower, "/p=")) {
      profile.port = ParsePort(token.substr(3), profile.port);
    } else if (StartsWithInsensitive(lower, "/user=")) {
      profile.username = token.substr(6);
    } else if (StartsWithInsensitive(lower, "/passwd=")) {
      profile.password = token.substr(8);
    } else if (StartsWithInsensitive(lower, "/keyfile=")) {
      profile.private_key_path = token.substr(9);
    } else if (StartsWithInsensitive(lower, "/publickey=")) {
      profile.public_key_path = token.substr(11);
    } else if (StartsWithInsensitive(lower, "/passphrase=")) {
      profile.passphrase = token.substr(12);
    } else if (StartsWithInsensitive(lower, "-proxy=") || StartsWithInsensitive(lower, "/proxy=")) {
      ParseProxyUrl(token.substr(7), &profile);
    } else if (lower == "-noproxy" || lower == "/noproxy") {
      profile.proxy = ProxyOpenOptions{};
    } else if (StartsWithInsensitive(lower, "/f=")) {
      profile.setup_file = token.substr(3);
    } else if (StartsWithInsensitive(lower, "/m=")) {
      profile.macro_file = token.substr(3);
    } else if (StartsWithInsensitive(lower, "/c=")) {
      profile.protocol = ProfileProtocol::Serial;
      profile.serial.device_path = SerialDevicePathFromComSpec(token.substr(3));
      profile.host = profile.serial.device_path;
      profile.port = 0;
      profile.warnings.push_back("serial setting was imported as a POSIX device path; verify it on HarmonyOS PC");
    } else if (StartsWithInsensitive(lower, "/baud=")) {
      profile.serial.baud_rate = ParseUnsigned(token.substr(6), profile.serial.baud_rate, 1, 4000000);
    } else if (StartsWithInsensitive(lower, "/speed=")) {
      profile.serial.baud_rate = ParseUnsigned(token.substr(7), profile.serial.baud_rate, 1, 4000000);
    } else if (StartsWithInsensitive(lower, "/cdatabit=")) {
      profile.serial.data_bits = ParseDataBits(token.substr(10), profile.serial.data_bits);
    } else if (StartsWithInsensitive(lower, "/cparity=")) {
      const std::string parity = Lower(token.substr(9));
      if (parity == "mark" || parity == "space") {
        profile.warnings.push_back(parity + " serial parity is not supported by the HarmonyOS seed yet");
        profile.serial.parity = SerialParity::None;
      } else {
        profile.serial.parity = SerialParityFromString(parity);
      }
    } else if (StartsWithInsensitive(lower, "/cstopbit=")) {
      profile.serial.stop_bits = ParseStopBits(token.substr(10), profile.serial.stop_bits);
    } else if (StartsWithInsensitive(lower, "/cflowctrl=")) {
      const std::string flow = Lower(token.substr(11));
      if (flow == "dsrdtr") {
        profile.warnings.push_back("DSR/DTR flow control was mapped to hardware flow control");
      }
      profile.serial.flow_control = SerialFlowControlFromString(flow);
    } else if (StartsWithInsensitive(lower, "/r=")) {
      profile.protocol = ProfileProtocol::Unsupported;
      profile.warnings.push_back("replay profiles are not part of the first HarmonyOS PC target");
    } else if (StartsWithInsensitive(token, "\\\\")) {
      profile.protocol = ProfileProtocol::Unsupported;
      profile.host = token;
      profile.warnings.push_back("named pipe profiles are Windows-specific");
    } else if (!StartsWithInsensitive(lower, "/")) {
      ParseHostPort(token, &profile);
    }
  }

  if (profile.protocol == ProfileProtocol::Ssh && profile.port == 23) {
    profile.port = 22;
  }
  if (profile.protocol == ProfileProtocol::Serial) {
    if (profile.host.empty()) {
      profile.host = profile.serial.device_path;
    }
    if (profile.serial.device_path.empty()) {
      profile.warnings.push_back("serial device path could not be parsed");
    }
  } else if (profile.host.empty() && profile.protocol != ProfileProtocol::Unsupported) {
    profile.warnings.push_back("host could not be parsed");
  }
  return profile;
}

}  // namespace

std::vector<ConnectionProfile> TeraTermIniImporter::ImportHostProfiles(const std::string& ini_text) {
  const IniData data = ParseIni(ini_text);
  std::vector<ConnectionProfile> profiles;

  const auto section_iter = data.sections.find("hosts");
  if (section_iter == data.sections.end()) {
    return profiles;
  }

  for (const auto& [key, value] : section_iter->second) {
    if (StartsWithInsensitive(key, "host")) {
      profiles.push_back(ParseHostEntry(data, key, value));
    }
  }

  std::sort(profiles.begin(), profiles.end(), [](const ConnectionProfile& lhs, const ConnectionProfile& rhs) {
    const int left = std::atoi(lhs.name.c_str() + 4);
    const int right = std::atoi(rhs.name.c_str() + 4);
    if (left == right) {
      return lhs.name < rhs.name;
    }
    return left < right;
  });
  return profiles;
}

std::string ProfileProtocolName(ProfileProtocol protocol) {
  switch (protocol) {
    case ProfileProtocol::Tcp:
      return "tcp";
    case ProfileProtocol::Telnet:
      return "telnet";
    case ProfileProtocol::Ssh:
      return "ssh";
    case ProfileProtocol::Serial:
      return "serial";
    case ProfileProtocol::Unsupported:
      return "unsupported";
  }
  return "unsupported";
}

}  // namespace teraterm::ohos
