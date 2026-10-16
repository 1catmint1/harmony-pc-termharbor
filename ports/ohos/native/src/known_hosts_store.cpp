#include "known_hosts_store.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace teraterm::ohos {
namespace {

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

std::vector<std::string> Split(const std::string& value, char delimiter) {
  std::vector<std::string> result;
  std::stringstream stream(value);
  std::string item;
  while (std::getline(stream, item, delimiter)) {
    if (!item.empty()) {
      result.push_back(item);
    }
  }
  return result;
}

std::vector<std::string> SplitWords(const std::string& value) {
  std::vector<std::string> result;
  std::stringstream stream(value);
  std::string item;
  while (stream >> item) {
    result.push_back(item);
  }
  return result;
}

bool WildcardMatch(const std::string& pattern, const std::string& value) {
  const std::string lower_pattern = Lower(pattern);
  const std::string lower_value = Lower(value);
  std::size_t pattern_index = 0;
  std::size_t value_index = 0;
  std::size_t star_index = std::string::npos;
  std::size_t match_index = 0;

  while (value_index < lower_value.size()) {
    if (pattern_index < lower_pattern.size() &&
        (lower_pattern[pattern_index] == '?' ||
         lower_pattern[pattern_index] == lower_value[value_index])) {
      ++pattern_index;
      ++value_index;
    } else if (pattern_index < lower_pattern.size() && lower_pattern[pattern_index] == '*') {
      star_index = pattern_index++;
      match_index = value_index;
    } else if (star_index != std::string::npos) {
      pattern_index = star_index + 1;
      value_index = ++match_index;
    } else {
      return false;
    }
  }

  while (pattern_index < lower_pattern.size() && lower_pattern[pattern_index] == '*') {
    ++pattern_index;
  }
  return pattern_index == lower_pattern.size();
}

std::string HostPortPattern(const std::string& host, uint16_t port) {
  if (port == 22) {
    return host;
  }
  return "[" + host + "]:" + std::to_string(port);
}

bool HostListMatches(const std::vector<std::string>& hosts, const std::string& host, uint16_t port) {
  const std::string host_only = Lower(host);
  const std::string host_port = Lower(HostPortPattern(host, port));
  bool matched = false;

  for (std::string pattern : hosts) {
    bool negated = false;
    if (!pattern.empty() && pattern.front() == '!') {
      negated = true;
      pattern.erase(pattern.begin());
    }
    const std::string lowered_pattern = Lower(pattern);
    const bool item_matches = WildcardMatch(lowered_pattern, host_only) ||
                              WildcardMatch(lowered_pattern, host_port);
    if (item_matches && negated) {
      return false;
    }
    if (item_matches) {
      matched = true;
    }
  }
  return matched;
}

KnownHostEntry ParseLine(const std::string& raw_line) {
  KnownHostEntry entry;
  entry.raw_line = raw_line;

  std::string line = Trim(raw_line);
  if (line.empty() || line.front() == '#') {
    return entry;
  }

  const std::vector<std::string> words = SplitWords(line);
  std::size_t index = 0;
  if (!words.empty() && !words[0].empty() && words[0].front() == '@') {
    entry.marker = words[0];
    index = 1;
  }
  if (index + 2 >= words.size()) {
    entry.warnings.push_back("known_hosts line is incomplete");
    return entry;
  }

  entry.hosts = Split(words[index], ',');
  entry.key_type = words[index + 1];
  entry.key_base64 = words[index + 2];

  if (entry.hosts.size() == 1 && entry.hosts[0].rfind("|1|", 0) == 0) {
    entry.warnings.push_back("hashed host entries are preserved but not matchable without HMAC-SHA1 support");
  }

  if (index + 3 < words.size()) {
    for (std::size_t i = index + 3; i < words.size(); ++i) {
      if (!entry.comment.empty()) {
        entry.comment += " ";
      }
      entry.comment += words[i];
    }
  }
  return entry;
}

std::string EntryLine(const KnownHostEntry& entry) {
  std::string line;
  if (!entry.marker.empty()) {
    line += entry.marker + " ";
  }
  for (std::size_t i = 0; i < entry.hosts.size(); ++i) {
    if (i != 0) {
      line += ",";
    }
    line += entry.hosts[i];
  }
  line += " " + entry.key_type + " " + entry.key_base64;
  if (!entry.comment.empty()) {
    line += " " + entry.comment;
  }
  return line;
}

}  // namespace

std::vector<KnownHostEntry> KnownHostsStore::Parse(const std::string& known_hosts_text) {
  std::vector<KnownHostEntry> entries;
  std::stringstream stream(known_hosts_text);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    KnownHostEntry entry = ParseLine(line);
    if (!entry.hosts.empty() || !entry.warnings.empty()) {
      entries.push_back(std::move(entry));
    }
  }
  return entries;
}

KnownHostMatch KnownHostsStore::Match(const std::vector<KnownHostEntry>& entries,
                                      const std::string& host,
                                      uint16_t port,
                                      const std::string& key_type,
                                      const std::string& key_base64) {
  KnownHostMatch result;
  for (const KnownHostEntry& entry : entries) {
    if (!entry.warnings.empty() &&
        entry.warnings[0].find("hashed host entries") != std::string::npos) {
      result.status = KnownHostMatchStatus::Unsupported;
      result.message = entry.warnings[0];
      continue;
    }
    if (!HostListMatches(entry.hosts, host, port)) {
      continue;
    }
    result.entry = entry;
    if (entry.key_type == key_type && entry.key_base64 == key_base64) {
      result.status = KnownHostMatchStatus::Trusted;
      result.message = "host key is trusted";
    } else {
      result.status = KnownHostMatchStatus::Changed;
      result.message = "host key changed";
    }
    return result;
  }

  if (result.status != KnownHostMatchStatus::Unsupported) {
    result.status = KnownHostMatchStatus::NotFound;
    result.message = "host key was not found";
  }
  return result;
}

std::string KnownHostsStore::AddOrUpdate(const std::string& known_hosts_text,
                                         const std::string& host,
                                         uint16_t port,
                                         const std::string& key_type,
                                         const std::string& key_base64,
                                         const std::string& comment) {
  std::vector<KnownHostEntry> entries = Parse(known_hosts_text);
  const std::string host_pattern = HostPortPattern(host, port);
  bool updated = false;

  for (KnownHostEntry& entry : entries) {
    if (!entry.warnings.empty() &&
        entry.warnings[0].find("hashed host entries") != std::string::npos) {
      continue;
    }
    if (!HostListMatches(entry.hosts, host, port)) {
      continue;
    }
    entry.hosts = {host_pattern};
    entry.key_type = key_type;
    entry.key_base64 = key_base64;
    entry.comment = comment;
    updated = true;
    break;
  }

  if (!updated) {
    KnownHostEntry entry;
    entry.hosts = {host_pattern};
    entry.key_type = key_type;
    entry.key_base64 = key_base64;
    entry.comment = comment;
    entries.push_back(std::move(entry));
  }

  std::string output;
  for (const KnownHostEntry& entry : entries) {
    if (entry.hosts.empty() || entry.key_type.empty() || entry.key_base64.empty()) {
      if (!entry.raw_line.empty()) {
        output += entry.raw_line;
        output += "\n";
      }
      continue;
    }
    output += EntryLine(entry);
    output += "\n";
  }
  return output;
}

std::string KnownHostMatchStatusName(KnownHostMatchStatus status) {
  switch (status) {
    case KnownHostMatchStatus::Trusted:
      return "trusted";
    case KnownHostMatchStatus::Changed:
      return "changed";
    case KnownHostMatchStatus::NotFound:
      return "not_found";
    case KnownHostMatchStatus::Unsupported:
      return "unsupported";
  }
  return "unsupported";
}

}  // namespace teraterm::ohos
