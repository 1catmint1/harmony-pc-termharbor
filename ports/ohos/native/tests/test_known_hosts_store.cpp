#include "known_hosts_store.h"

#include <cassert>
#include <string>

using teraterm::ohos::KnownHostMatchStatus;
using teraterm::ohos::KnownHostsStore;

int main() {
  const std::string text =
      "example.com ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIE trusted\n"
      "[example.net]:10022 ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQ remote\n"
      "*.internal,!bad.internal ecdsa-sha2-nistp256 AAAAE2VjZHNhLXNoYTItbmlzdHAyNTY good\n"
      "|1|salt|hash ssh-ed25519 AAAAhash hidden\n";

  const auto entries = KnownHostsStore::Parse(text);
  assert(entries.size() == 4);
  assert(entries[0].hosts[0] == "example.com");
  assert(entries[0].key_type == "ssh-ed25519");

  auto match = KnownHostsStore::Match(entries, "example.com", 22, "ssh-ed25519",
                                      "AAAAC3NzaC1lZDI1NTE5AAAAIE");
  assert(match.status == KnownHostMatchStatus::Trusted);

  match = KnownHostsStore::Match(entries, "example.com", 22, "ssh-ed25519", "different");
  assert(match.status == KnownHostMatchStatus::Changed);

  match = KnownHostsStore::Match(entries, "example.net", 10022, "ssh-rsa",
                                 "AAAAB3NzaC1yc2EAAAADAQABAAABAQ");
  assert(match.status == KnownHostMatchStatus::Trusted);

  match = KnownHostsStore::Match(entries, "build.internal", 22, "ecdsa-sha2-nistp256",
                                 "AAAAE2VjZHNhLXNoYTItbmlzdHAyNTY");
  assert(match.status == KnownHostMatchStatus::Trusted);

  match = KnownHostsStore::Match(entries, "bad.internal", 22, "ecdsa-sha2-nistp256",
                                 "AAAAE2VjZHNhLXNoYTItbmlzdHAyNTY");
  assert(match.status == KnownHostMatchStatus::Unsupported);

  const auto plain_entries = KnownHostsStore::Parse("example.com ssh-ed25519 AAAA trusted\n");
  match = KnownHostsStore::Match(plain_entries, "missing.example", 22, "ssh-ed25519", "AAAA");
  assert(match.status == KnownHostMatchStatus::NotFound);

  const std::string added = KnownHostsStore::AddOrUpdate(text, "new.example", 2222, "ssh-ed25519",
                                                         "AAAnew", "added");
  assert(added.find("[new.example]:2222 ssh-ed25519 AAAnew added") != std::string::npos);

  const std::string updated = KnownHostsStore::AddOrUpdate(text, "example.com", 22, "ssh-ed25519",
                                                           "AAAreplaced", "updated");
  assert(updated.find("example.com ssh-ed25519 AAAreplaced updated") != std::string::npos);
  assert(updated.find("AAAAC3NzaC1lZDI1NTE5AAAAIE trusted") == std::string::npos);
  return 0;
}
