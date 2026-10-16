#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace teraterm::ohos {

struct KnownHostEntry {
  std::string marker;
  std::vector<std::string> hosts;
  std::string key_type;
  std::string key_base64;
  std::string comment;
  std::string raw_line;
  std::vector<std::string> warnings;
};

enum class KnownHostMatchStatus {
  Trusted,
  Changed,
  NotFound,
  Unsupported,
};

struct KnownHostMatch {
  KnownHostMatchStatus status = KnownHostMatchStatus::NotFound;
  KnownHostEntry entry;
  std::string message;
};

class KnownHostsStore {
public:
  static std::vector<KnownHostEntry> Parse(const std::string& known_hosts_text);
  static KnownHostMatch Match(const std::vector<KnownHostEntry>& entries,
                              const std::string& host,
                              uint16_t port,
                              const std::string& key_type,
                              const std::string& key_base64);
  static std::string AddOrUpdate(const std::string& known_hosts_text,
                                 const std::string& host,
                                 uint16_t port,
                                 const std::string& key_type,
                                 const std::string& key_base64,
                                 const std::string& comment);
};

std::string KnownHostMatchStatusName(KnownHostMatchStatus status);

}  // namespace teraterm::ohos
