// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <functional>
#include <string>
namespace teraterm::ohos {
// SCP completes a file with a zero status byte in both directions.
// The transport callback performs one byte of read or write; retry follows
// the underlying SSH session's socket readiness rather than busy waiting.
inline bool CompleteScpFile(bool upload,
    const std::function<long(char*,bool)>& transfer,
    const std::function<bool()>& wait,
    long again,std::string* error) {
  for(bool writing:{upload,!upload}) {
    char status=0;
    while(true) {
      long count=transfer(&status,writing);
      if(count==again){if(wait())continue;return false;}
      if(count!=1){if(error)*error="SCP completion status was not received or sent";return false;}
      if(!writing&&status!=0){if(error)*error="Remote SCP endpoint rejected the completed file";return false;}
      break;
    }
  }
  return true;
}
}
