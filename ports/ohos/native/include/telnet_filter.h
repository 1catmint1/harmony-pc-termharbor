#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace teraterm::ohos {

class TelnetFilter {
public:
  void ProcessInbound(const std::vector<uint8_t>& input,
                      std::vector<uint8_t>* terminal_output,
                      std::vector<uint8_t>* negotiation_reply);
  std::vector<uint8_t> ProcessOutbound(const std::vector<uint8_t>& input) const;
  std::vector<uint8_t> BuildResizeCommand(std::size_t rows, std::size_t cols) const;

private:
  enum class State {
    Data,
    Iac,
    CommandOption,
    Subnegotiation,
    SubnegotiationIac,
  };

  enum class PendingCommand : uint8_t {
    None = 0,
    Will = 251,
    Wont = 252,
    Do = 253,
    Dont = 254,
  };

  void Reply(uint8_t command, uint8_t option, std::vector<uint8_t>* negotiation_reply) const;
  void HandleOption(uint8_t option, std::vector<uint8_t>* negotiation_reply);
  void HandleSubnegotiation(std::vector<uint8_t>* negotiation_reply);

  State state_ = State::Data;
  PendingCommand pending_command_ = PendingCommand::None;
  std::vector<uint8_t> subnegotiation_;
};

}  // namespace teraterm::ohos
