#include "telnet_filter.h"

namespace teraterm::ohos {
namespace {

constexpr uint8_t kSe = 240;
constexpr uint8_t kSb = 250;
constexpr uint8_t kWill = 251;
constexpr uint8_t kWont = 252;
constexpr uint8_t kDo = 253;
constexpr uint8_t kDont = 254;
constexpr uint8_t kIac = 255;

constexpr uint8_t kTransmitBinary = 0;
constexpr uint8_t kEcho = 1;
constexpr uint8_t kSuppressGoAhead = 3;
constexpr uint8_t kTerminalType = 24;
constexpr uint8_t kNaws = 31;

constexpr uint8_t kTerminalTypeIs = 0;
constexpr uint8_t kTerminalTypeSend = 1;

bool ShouldAcceptServerWill(uint8_t option) {
  return option == kEcho || option == kSuppressGoAhead || option == kTransmitBinary;
}

bool ShouldOfferClientWill(uint8_t option) {
  return option == kSuppressGoAhead || option == kTransmitBinary ||
         option == kTerminalType || option == kNaws;
}

void AppendIac(std::vector<uint8_t>* output, uint8_t command, uint8_t option) {
  output->push_back(kIac);
  output->push_back(command);
  output->push_back(option);
}

void AppendEscaped(std::vector<uint8_t>* output, uint8_t byte) {
  output->push_back(byte);
  if (byte == kIac) {
    output->push_back(kIac);
  }
}

}  // namespace

void TelnetFilter::ProcessInbound(const std::vector<uint8_t>& input,
                                  std::vector<uint8_t>* terminal_output,
                                  std::vector<uint8_t>* negotiation_reply) {
  for (const uint8_t byte : input) {
    switch (state_) {
      case State::Data:
        if (byte == kIac) {
          state_ = State::Iac;
        } else {
          terminal_output->push_back(byte);
        }
        break;
      case State::Iac:
        if (byte == kIac) {
          terminal_output->push_back(kIac);
          state_ = State::Data;
        } else if (byte == kWill || byte == kWont || byte == kDo || byte == kDont) {
          pending_command_ = static_cast<PendingCommand>(byte);
          state_ = State::CommandOption;
        } else if (byte == kSb) {
          subnegotiation_.clear();
          state_ = State::Subnegotiation;
        } else {
          state_ = State::Data;
        }
        break;
      case State::CommandOption:
        HandleOption(byte, negotiation_reply);
        state_ = State::Data;
        pending_command_ = PendingCommand::None;
        break;
      case State::Subnegotiation:
        if (byte == kIac) {
          state_ = State::SubnegotiationIac;
        } else {
          subnegotiation_.push_back(byte);
        }
        break;
      case State::SubnegotiationIac:
        if (byte == kIac) {
          subnegotiation_.push_back(kIac);
          state_ = State::Subnegotiation;
        } else if (byte == kSe) {
          HandleSubnegotiation(negotiation_reply);
          subnegotiation_.clear();
          state_ = State::Data;
        } else {
          state_ = State::Data;
        }
        break;
    }
  }
}

std::vector<uint8_t> TelnetFilter::ProcessOutbound(const std::vector<uint8_t>& input) const {
  std::vector<uint8_t> output;
  output.reserve(input.size());
  for (const uint8_t byte : input) {
    AppendEscaped(&output, byte);
  }
  return output;
}

std::vector<uint8_t> TelnetFilter::BuildResizeCommand(std::size_t rows, std::size_t cols) const {
  std::vector<uint8_t> output;
  output.reserve(9);
  output.push_back(kIac);
  output.push_back(kSb);
  output.push_back(kNaws);
  AppendEscaped(&output, static_cast<uint8_t>((cols >> 8) & 0xff));
  AppendEscaped(&output, static_cast<uint8_t>(cols & 0xff));
  AppendEscaped(&output, static_cast<uint8_t>((rows >> 8) & 0xff));
  AppendEscaped(&output, static_cast<uint8_t>(rows & 0xff));
  output.push_back(kIac);
  output.push_back(kSe);
  return output;
}

void TelnetFilter::Reply(uint8_t command, uint8_t option, std::vector<uint8_t>* negotiation_reply) const {
  AppendIac(negotiation_reply, command, option);
}

void TelnetFilter::HandleOption(uint8_t option, std::vector<uint8_t>* negotiation_reply) {
  switch (pending_command_) {
    case PendingCommand::Will:
      Reply(ShouldAcceptServerWill(option) ? kDo : kDont, option, negotiation_reply);
      break;
    case PendingCommand::Do:
      Reply(ShouldOfferClientWill(option) ? kWill : kWont, option, negotiation_reply);
      break;
    case PendingCommand::Wont:
    case PendingCommand::Dont:
    case PendingCommand::None:
      break;
  }
}

void TelnetFilter::HandleSubnegotiation(std::vector<uint8_t>* negotiation_reply) {
  if (subnegotiation_.size() < 2) {
    return;
  }
  if (subnegotiation_[0] == kTerminalType && subnegotiation_[1] == kTerminalTypeSend) {
    negotiation_reply->push_back(kIac);
    negotiation_reply->push_back(kSb);
    negotiation_reply->push_back(kTerminalType);
    negotiation_reply->push_back(kTerminalTypeIs);
    negotiation_reply->push_back('V');
    negotiation_reply->push_back('T');
    negotiation_reply->push_back('1');
    negotiation_reply->push_back('0');
    negotiation_reply->push_back('0');
    negotiation_reply->push_back(kIac);
    negotiation_reply->push_back(kSe);
  }
}

}  // namespace teraterm::ohos
