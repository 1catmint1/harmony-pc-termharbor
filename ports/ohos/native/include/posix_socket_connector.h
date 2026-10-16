#pragma once

#include "session_transport.h"

#include <string>

namespace teraterm::ohos {

int ConnectTcpSocket(const TransportOpenOptions& options, std::string* error);
bool WaitForSocketWritable(int fd, int timeout_ms, std::string* error);
bool WaitForSocketReadable(int fd, int timeout_ms, std::string* error);
std::string LastSocketError(const char* action);

}  // namespace teraterm::ohos
