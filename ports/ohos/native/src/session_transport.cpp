#include "session_transport.h"

#include <algorithm>
#include <cctype>

namespace teraterm::ohos {
namespace {

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

}  // namespace

std::string ProxyTypeName(ProxyType type) {
  switch (type) {
    case ProxyType::None:
      return "none";
    case ProxyType::HttpConnect:
      return "http";
    case ProxyType::Telnet:
      return "telnet";
    case ProxyType::Socks4:
      return "socks4";
    case ProxyType::Socks5:
      return "socks5";
  }
  return "none";
}

ProxyType ProxyTypeFromString(const std::string& value) {
  const std::string lower = Lower(value);
  if (lower == "http" || lower == "http_connect" || lower == "http-connect") {
    return ProxyType::HttpConnect;
  }
  if (lower == "telnet") {
    return ProxyType::Telnet;
  }
  if (lower == "socks4" || lower == "socks4a") {
    return ProxyType::Socks4;
  }
  if (lower == "socks5" || lower == "socks") {
    return ProxyType::Socks5;
  }
  return ProxyType::None;
}

std::string SerialParityName(SerialParity parity) {
  switch (parity) {
    case SerialParity::None:
      return "none";
    case SerialParity::Odd:
      return "odd";
    case SerialParity::Even:
      return "even";
  }
  return "none";
}

SerialParity SerialParityFromString(const std::string& value) {
  const std::string lower = Lower(value);
  if (lower == "odd") {
    return SerialParity::Odd;
  }
  if (lower == "even") {
    return SerialParity::Even;
  }
  return SerialParity::None;
}

std::string SerialFlowControlName(SerialFlowControl flow_control) {
  switch (flow_control) {
    case SerialFlowControl::None:
      return "none";
    case SerialFlowControl::Software:
      return "software";
    case SerialFlowControl::Hardware:
      return "hardware";
  }
  return "none";
}

SerialFlowControl SerialFlowControlFromString(const std::string& value) {
  const std::string lower = Lower(value);
  if (lower == "x" || lower == "xonxoff" || lower == "software") {
    return SerialFlowControl::Software;
  }
  if (lower == "hard" || lower == "hardware" || lower == "rtscts" || lower == "dsrdtr") {
    return SerialFlowControl::Hardware;
  }
  return SerialFlowControl::None;
}

}  // namespace teraterm::ohos
