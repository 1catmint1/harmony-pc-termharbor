#pragma once

#include "session_transport.h"

namespace teraterm::ohos {

class PosixSerialTransport final : public SessionTransport {
public:
  PosixSerialTransport();
  ~PosixSerialTransport() override;

  bool Connect(const TransportOpenOptions& options, std::string* error) override;
  bool ConnectSerial(const SerialOpenOptions& options, std::string* error);
  void Close() override;
  bool IsOpen() const override;
  TransportReadResult Read(std::size_t max_bytes) override;
  bool Write(const uint8_t* data, std::size_t size, std::string* error) override;

private:
  int fd_ = -1;
};

}  // namespace teraterm::ohos
