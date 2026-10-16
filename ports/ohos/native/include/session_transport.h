#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace teraterm::ohos {

enum class ProxyType {
  None,
  HttpConnect,
  Telnet,
  Socks4,
  Socks5,
};

enum class SerialParity {
  None,
  Odd,
  Even,
};

enum class SerialFlowControl {
  None,
  Software,
  Hardware,
};

struct ProxyOpenOptions {
  ProxyType type = ProxyType::None;
  std::string host;
  uint16_t port = 0;
  std::string username;
  std::string password;
  std::string telnet_hostname_prompt = ">> Host name: ";
  std::string telnet_username_prompt = "Username:";
  std::string telnet_password_prompt = "Password:";
  std::string telnet_connected_message = "-- Connected to ";
  std::string telnet_error_message = "!!!!!!!!";
};

struct TransportOpenOptions {
  std::string host;
  uint16_t port = 0;
  int timeout_ms = 5000;
  ProxyOpenOptions proxy;
};

struct SerialOpenOptions {
  std::string device_path;
  uint32_t baud_rate = 9600;
  uint8_t data_bits = 8;
  uint8_t stop_bits = 1;
  SerialParity parity = SerialParity::None;
  SerialFlowControl flow_control = SerialFlowControl::None;
  int timeout_ms = 5000;
};

enum class TransportReadStatus {
  Data,
  WouldBlock,
  Closed,
  Error,
};

struct TransportReadResult {
  TransportReadStatus status = TransportReadStatus::WouldBlock;
  std::vector<uint8_t> data;
  std::string error;
};

class SessionTransport {
public:
  virtual ~SessionTransport() = default;

  virtual bool Connect(const TransportOpenOptions& options, std::string* error) = 0;
  virtual void Close() = 0;
  virtual bool IsOpen() const = 0;
  virtual TransportReadResult Read(std::size_t max_bytes) = 0;
  virtual bool Write(const uint8_t* data, std::size_t size, std::string* error) = 0;
};

std::string ProxyTypeName(ProxyType type);
ProxyType ProxyTypeFromString(const std::string& value);
std::string SerialParityName(SerialParity parity);
SerialParity SerialParityFromString(const std::string& value);
std::string SerialFlowControlName(SerialFlowControl flow_control);
SerialFlowControl SerialFlowControlFromString(const std::string& value);

}  // namespace teraterm::ohos
