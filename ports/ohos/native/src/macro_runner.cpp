#include "macro_runner.h"

#include "terminal_session.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <regex>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

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

std::string StripComment(const std::string& line) {
  bool in_single = false;
  bool in_double = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '\'' && !in_double) {
      in_single = !in_single;
    } else if (line[i] == '"' && !in_single) {
      in_double = !in_double;
    } else if (line[i] == ';' && !in_single && !in_double) {
      return line.substr(0, i);
    }
  }
  return line;
}

std::vector<std::string> SplitLines(const std::string& script) {
  std::vector<std::string> lines;
  std::stringstream stream(script);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  return lines;
}

std::string DecodeTokenFragment(const std::string& token) {
  std::string result;
  for (std::size_t i = 0; i < token.size();) {
    if (token[i] == '#' && i + 1 < token.size() &&
        std::isdigit(static_cast<unsigned char>(token[i + 1])) != 0) {
      std::size_t end = i + 1;
      while (end < token.size() && std::isdigit(static_cast<unsigned char>(token[end])) != 0) {
        ++end;
      }
      const int value = std::atoi(token.substr(i + 1, end - i - 1).c_str());
      if (value >= 0 && value <= 255) {
        result.push_back(static_cast<char>(value));
      }
      i = end;
    } else {
      result.push_back(token[i++]);
    }
  }
  return result;
}

std::vector<std::string> Tokenize(const std::string& line) {
  std::vector<std::string> tokens;
  std::string token;
  bool in_single = false;
  bool in_double = false;

  for (std::size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (ch == '\'' && !in_double) {
      in_single = !in_single;
      continue;
    }
    if (ch == '"' && !in_single) {
      in_double = !in_double;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(ch)) != 0 && !in_single && !in_double) {
      if (!token.empty()) {
        tokens.push_back(DecodeTokenFragment(token));
        token.clear();
      }
      continue;
    }
    token.push_back(ch);
  }
  if (!token.empty()) {
    tokens.push_back(DecodeTokenFragment(token));
  }
  return tokens;
}

std::string JoinArguments(const std::vector<std::string>& tokens, std::size_t begin, const std::string& delimiter) {
  std::string result;
  for (std::size_t i = begin; i < tokens.size(); ++i) {
    if (!result.empty()) {
      result += delimiter;
    }
    result += tokens[i];
  }
  return result;
}

std::string ProfileToIniHost(const std::string& spec) {
  return "[Hosts]\nHost1=" + spec + "\n";
}

std::string NormalizeName(const std::string& value) {
  std::string name = Lower(Trim(value));
  if (!name.empty() && name.front() == ':') {
    name.erase(name.begin());
  }
  return name;
}

bool IsIdentifierStart(char ch) {
  return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool IsIdentifierChar(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool IsIdentifier(const std::string& value) {
  if (value.empty() || !IsIdentifierStart(value.front())) {
    return false;
  }
  return std::all_of(value.begin() + 1, value.end(), IsIdentifierChar);
}

bool IsOperatorChar(char ch) {
  return ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '%' || ch == '&' ||
         ch == '<' || ch == '>' || ch == '=' || ch == '!' ||
         ch == '(' || ch == '[';
}

bool HasBinaryLeftOperand(const std::string& value, std::size_t index) {
  if (index == 0) {
    return false;
  }
  for (std::size_t pos = index; pos > 0; --pos) {
    const char ch = value[pos - 1];
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      continue;
    }
    return !IsOperatorChar(ch);
  }
  return false;
}

std::size_t FindTopLevelOperatorRight(const std::string& value, const std::string& operators) {
  bool in_single = false;
  bool in_double = false;
  int bracket_depth = 0;
  int paren_depth = 0;
  std::size_t found = std::string::npos;
  for (std::size_t index = 0; index < value.size(); ++index) {
    const char ch = value[index];
    if (ch == '\'' && !in_double) {
      in_single = !in_single;
      continue;
    }
    if (ch == '"' && !in_single) {
      in_double = !in_double;
      continue;
    }
    if (in_single || in_double) {
      continue;
    }
    if (ch == '[') {
      ++bracket_depth;
      continue;
    }
    if (ch == ']' && bracket_depth > 0) {
      --bracket_depth;
      continue;
    }
    if (ch == '(') {
      ++paren_depth;
      continue;
    }
    if (ch == ')' && paren_depth > 0) {
      --paren_depth;
      continue;
    }
    if (bracket_depth == 0 && paren_depth == 0 &&
        operators.find(ch) != std::string::npos && HasBinaryLeftOperand(value, index)) {
      found = index;
    }
  }
  return found;
}

bool HasWrappingParentheses(const std::string& value) {
  const std::string trimmed = Trim(value);
  if (trimmed.size() < 2 || trimmed.front() != '(' || trimmed.back() != ')') {
    return false;
  }

  bool in_single = false;
  bool in_double = false;
  int depth = 0;
  for (std::size_t index = 0; index < trimmed.size(); ++index) {
    const char ch = trimmed[index];
    if (ch == '\'' && !in_double) {
      in_single = !in_single;
      continue;
    }
    if (ch == '"' && !in_single) {
      in_double = !in_double;
      continue;
    }
    if (in_single || in_double) {
      continue;
    }
    if (ch == '(') {
      ++depth;
    } else if (ch == ')') {
      --depth;
      if (depth == 0 && index + 1 < trimmed.size()) {
        return false;
      }
    }
  }
  return depth == 0;
}

bool ParseArrayReference(const std::string& value, std::string* name, std::string* index_expression) {
  const std::string trimmed = Trim(value);
  bool in_single = false;
  bool in_double = false;
  std::size_t open = std::string::npos;
  int depth = 0;
  for (std::size_t index = 0; index < trimmed.size(); ++index) {
    const char ch = trimmed[index];
    if (ch == '\'' && !in_double) {
      in_single = !in_single;
      continue;
    }
    if (ch == '"' && !in_single) {
      in_double = !in_double;
      continue;
    }
    if (in_single || in_double) {
      continue;
    }
    if (ch == '[') {
      if (depth == 0) {
        open = index;
      }
      ++depth;
    } else if (ch == ']') {
      --depth;
      if (depth == 0 && index + 1 != trimmed.size()) {
        return false;
      }
    }
  }
  if (open == std::string::npos || depth != 0 || trimmed.back() != ']') {
    return false;
  }

  const std::string array_name = Trim(trimmed.substr(0, open));
  if (!IsIdentifier(array_name)) {
    return false;
  }
  if (name != nullptr) {
    *name = NormalizeName(array_name);
  }
  if (index_expression != nullptr) {
    *index_expression = trimmed.substr(open + 1, trimmed.size() - open - 2);
  }
  return true;
}

bool IsQuoted(const std::string& value) {
  const std::string trimmed = Trim(value);
  return trimmed.size() >= 2 &&
         ((trimmed.front() == '\'' && trimmed.back() == '\'') ||
          (trimmed.front() == '"' && trimmed.back() == '"'));
}

std::string UnquoteAndDecode(const std::string& value) {
  std::string trimmed = Trim(value);
  if (IsQuoted(trimmed)) {
    trimmed = trimmed.substr(1, trimmed.size() - 2);
  }
  return DecodeTokenFragment(trimmed);
}

bool IsIntegerLiteral(const std::string& value) {
  std::string trimmed = Trim(value);
  if (trimmed.empty()) {
    return false;
  }
  if (trimmed.front() == '$') {
    return trimmed.size() > 1 &&
           std::all_of(trimmed.begin() + 1, trimmed.end(), [](unsigned char ch) {
             return std::isxdigit(ch) != 0;
           });
  }
  std::size_t begin = (trimmed.front() == '-' || trimmed.front() == '+') ? 1 : 0;
  if (begin >= trimmed.size()) {
    return false;
  }
  return std::all_of(trimmed.begin() + begin, trimmed.end(), [](unsigned char ch) {
    return std::isdigit(ch) != 0;
  });
}

std::size_t FindOutsideQuotes(const std::string& value, const std::string& needle) {
  bool in_single = false;
  bool in_double = false;
  for (std::size_t i = 0; i + needle.size() <= value.size(); ++i) {
    if (value[i] == '\'' && !in_double) {
      in_single = !in_single;
    } else if (value[i] == '"' && !in_single) {
      in_double = !in_double;
    }
    if (!in_single && !in_double && value.compare(i, needle.size(), needle) == 0) {
      return i;
    }
  }
  return std::string::npos;
}

std::size_t FindAssignmentEquals(const std::string& value) {
  bool in_single = false;
  bool in_double = false;
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '\'' && !in_double) {
      in_single = !in_single;
      continue;
    }
    if (value[i] == '"' && !in_single) {
      in_double = !in_double;
      continue;
    }
    if (in_single || in_double || value[i] != '=') {
      continue;
    }
    const char previous = i > 0 ? value[i - 1] : '\0';
    const char next = i + 1 < value.size() ? value[i + 1] : '\0';
    if (previous == '<' || previous == '>' || previous == '!' || previous == '=' || next == '=') {
      continue;
    }
    return i;
  }
  return std::string::npos;
}

bool IsStatementKeyword(const std::string& token) {
  const std::string command = Lower(token);
  return command == "changedir" || command == "closesbox" || command == "connect" ||
         command == "dirnamebox" || command == "disconnect" || command == "end" ||
         command == "expandenv" ||
         command == "fileclose" || command == "fileconcat" || command == "filecopy" ||
         command == "filecreate" || command == "filedelete" || command == "filemarkptr" ||
         command == "filenamebox" || command == "filelock" ||
         command == "fileopen" || command == "fileread" || command == "filereadln" ||
         command == "filerename" || command == "filesearch" || command == "fileseek" ||
         command == "fileseekback" || command == "filestat" || command == "filestrseek" ||
         command == "filestrseek2" || command == "filetruncate" || command == "fileunlock" ||
         command == "filewrite" || command == "filewriteln" ||
         command == "findclose" || command == "findfirst" || command == "findnext" ||
         command == "foldercreate" || command == "folderdelete" || command == "foldersearch" ||
         command == "getdate" || command == "getdir" || command == "getenv" ||
         command == "gettime" || command == "getttdir" ||
         command == "goto" || command == "call" || command == "return" ||
         command == "inputbox" || command == "messagebox" || command == "passwordbox" ||
         command == "listbox" ||
         command == "logautoclosemode" || command == "logclose" || command == "loginfo" ||
         command == "logopen" || command == "logpause" || command == "logrotate" ||
         command == "logstart" || command == "logwrite" ||
         command == "mpause" || command == "pause" || command == "send" ||
         command == "sendln" || command == "setsync" || command == "yesnobox" ||
         command == "setdir" ||
         command == "statusbox" ||
         command == "str2int" || command == "strconcat" || command == "strlen" ||
         command == "int2str" || command == "wait" || command == "waitln" ||
         command == "waitregex" || command == "while" || command == "endwhile" ||
         command == "until" || command == "enduntil" || command == "do" ||
         command == "loop" || command == "for" || command == "next" ||
         command == "break" || command == "continue" || command == "intdim" ||
         command == "strdim" || command == "strcompare" || command == "strscan" ||
         command == "strcopy" || command == "strmatch" || command == "strreplace" ||
         command == "sprintf" || command == "sprintf2" || command == "uptime";
}

std::size_t ThenTokenIndex(const std::vector<std::string>& tokens) {
  for (std::size_t index = 1; index < tokens.size(); ++index) {
    if (Lower(tokens[index]) == "then") {
      return index;
    }
  }
  return std::string::npos;
}

bool IsBlockIfTokens(const std::vector<std::string>& tokens) {
  if (tokens.empty() || Lower(tokens[0]) != "if") {
    return false;
  }
  const std::size_t then_index = ThenTokenIndex(tokens);
  return then_index != std::string::npos && then_index + 1 >= tokens.size();
}

bool IsBlockIfLine(const std::string& line) {
  return IsBlockIfTokens(Tokenize(line));
}

std::string FirstCommand(const std::string& line) {
  const std::vector<std::string> tokens = Tokenize(line);
  return tokens.empty() ? "" : Lower(tokens[0]);
}

std::string JoinTokens(const std::vector<std::string>& tokens, std::size_t begin, std::size_t end) {
  std::string result;
  for (std::size_t index = begin; index < end && index < tokens.size(); ++index) {
    if (!result.empty()) {
      result += " ";
    }
    result += tokens[index];
  }
  return result;
}

bool LooksLikeAssignmentStatement(const std::string& token) {
  const std::size_t equals = FindAssignmentEquals(token);
  return equals != std::string::npos && IsIdentifier(token.substr(0, equals));
}

struct PrintfSpec {
  bool left = false;
  bool plus = false;
  bool space = false;
  bool alternate = false;
  bool zero = false;
  int width = 0;
  int precision = -1;
  char type = '\0';
};

std::string UnsignedToBase(unsigned int value, int base, bool upper) {
  const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  if (value == 0) {
    return "0";
  }
  std::string result;
  while (value > 0) {
    result.push_back(digits[value % static_cast<unsigned int>(base)]);
    value /= static_cast<unsigned int>(base);
  }
  std::reverse(result.begin(), result.end());
  return result;
}

std::string ApplyWidth(const std::string& value, const PrintfSpec& spec, bool numeric) {
  if (spec.width <= static_cast<int>(value.size())) {
    return value;
  }

  const std::size_t padding = static_cast<std::size_t>(spec.width) - value.size();
  if (spec.left) {
    return value + std::string(padding, ' ');
  }
  if (!spec.zero || !numeric || spec.precision >= 0) {
    return std::string(padding, ' ') + value;
  }

  std::size_t prefix = 0;
  if (!value.empty() && (value[0] == '-' || value[0] == '+' || value[0] == ' ')) {
    prefix = 1;
  } else if (value.size() > 1 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
    prefix = 2;
  }
  return value.substr(0, prefix) + std::string(padding, '0') + value.substr(prefix);
}

std::string FormatSigned(int value, const PrintfSpec& spec) {
  std::string digits = std::to_string(value < 0 ? -value : value);
  if (spec.precision > static_cast<int>(digits.size())) {
    digits = std::string(static_cast<std::size_t>(spec.precision) - digits.size(), '0') + digits;
  }
  if (value < 0) {
    digits = "-" + digits;
  } else if (spec.plus) {
    digits = "+" + digits;
  } else if (spec.space) {
    digits = " " + digits;
  }
  return ApplyWidth(digits, spec, true);
}

std::string FormatUnsigned(unsigned int value, int base, bool upper, const PrintfSpec& spec) {
  std::string digits = UnsignedToBase(value, base, upper);
  if (spec.precision > static_cast<int>(digits.size())) {
    digits = std::string(static_cast<std::size_t>(spec.precision) - digits.size(), '0') + digits;
  }
  if (spec.alternate && value != 0) {
    if (base == 16) {
      digits = std::string(upper ? "0X" : "0x") + digits;
    } else if (base == 8 && digits.front() != '0') {
      digits = "0" + digits;
    }
  }
  return ApplyWidth(digits, spec, true);
}

std::string FormatStringValue(std::string value, const PrintfSpec& spec) {
  if (spec.precision >= 0 && spec.precision < static_cast<int>(value.size())) {
    value.resize(static_cast<std::size_t>(spec.precision));
  }
  return ApplyWidth(value, spec, false);
}

std::string FormatDouble(double value, const PrintfSpec& spec) {
  std::ostringstream stream;
  if (spec.plus) {
    stream << std::showpos;
  }
  if (spec.type == 'e' || spec.type == 'E') {
    stream << std::scientific;
  } else if (spec.type == 'f') {
    stream << std::fixed;
  } else {
    stream << std::defaultfloat;
  }
  if (spec.precision >= 0) {
    stream << std::setprecision(spec.precision);
  }
  stream << value;
  std::string text = stream.str();
  if (spec.type == 'E' || spec.type == 'G') {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
      return static_cast<char>(std::toupper(ch));
    });
  }
  return ApplyWidth(text, spec, true);
}

std::string CurrentDirectoryString() {
  std::error_code error;
  std::filesystem::path current = std::filesystem::current_path(error);
  if (error) {
    current = ".";
  }
  return std::filesystem::absolute(current).lexically_normal().string();
}

bool IsWritableMacroTarget(const std::string& token) {
  std::string array_name;
  std::string index_expression;
  return IsIdentifier(token) || ParseArrayReference(token, &array_name, &index_expression);
}

std::string ExpandPercentEnvironmentVariables(const std::string& value) {
  std::string result;
  for (std::size_t index = 0; index < value.size();) {
    if (value[index] != '%') {
      result.push_back(value[index++]);
      continue;
    }

    const std::size_t end = value.find('%', index + 1);
    if (end == std::string::npos) {
      result.append(value.substr(index));
      break;
    }

    const std::string name = value.substr(index + 1, end - index - 1);
    const char* env_value = name.empty() ? nullptr : std::getenv(name.c_str());
    if (env_value == nullptr) {
      result.append(value.substr(index, end - index + 1));
    } else {
      result.append(env_value);
    }
    index = end + 1;
  }
  return result;
}

bool NormalizeStrftimeFormat(const std::string& format, std::string* normalized) {
  normalized->clear();
  for (std::size_t index = 0; index < format.size(); ++index) {
    if (format[index] != '%') {
      normalized->push_back(format[index]);
      continue;
    }
    if (index + 1 >= format.size()) {
      return false;
    }
    if (format[index + 1] == '#') {
      if (index + 2 >= format.size()) {
        return false;
      }
#if defined(_WIN32)
      normalized->append("%#");
#else
      normalized->append("%-");
#endif
      index += 2;
      normalized->push_back(format[index]);
      continue;
    }
    normalized->push_back('%');
    normalized->push_back(format[++index]);
  }
  return true;
}

bool FormatLocalDateTime(const std::string& format,
                         const std::string* timezone,
                         std::string* output,
                         int* result_code) {
  std::string normalized_format;
  if (!NormalizeStrftimeFormat(format, &normalized_format)) {
    *result_code = 2;
    return false;
  }

  const char* previous_tz_raw = std::getenv("TZ");
  const bool had_previous_tz = previous_tz_raw != nullptr;
  const std::string previous_tz = previous_tz_raw == nullptr ? "" : previous_tz_raw;
  if (timezone != nullptr) {
#if defined(_WIN32)
    _putenv_s("TZ", timezone->c_str());
    _tzset();
#else
    setenv("TZ", timezone->c_str(), 1);
    tzset();
#endif
  }

  const std::time_t now = std::time(nullptr);
  std::tm local_time = {};
#if defined(_WIN32)
  const bool time_ok = localtime_s(&local_time, &now) == 0;
#else
  const bool time_ok = localtime_r(&now, &local_time) != nullptr;
#endif

  char buffer[512] = {};
  const std::size_t written = time_ok ? std::strftime(buffer, sizeof(buffer), normalized_format.c_str(), &local_time) : 0;

  if (timezone != nullptr) {
#if defined(_WIN32)
    _putenv_s("TZ", had_previous_tz ? previous_tz.c_str() : "");
    _tzset();
#else
    if (had_previous_tz) {
      setenv("TZ", previous_tz.c_str(), 1);
    } else {
      unsetenv("TZ");
    }
    tzset();
#endif
  }

  if (written == 0) {
    *result_code = 1;
    return false;
  }

  *output = buffer;
  *result_code = 0;
  return true;
}

std::string StripAnsiSequences(const std::string& bytes) {
  std::string output;
  for (std::size_t index = 0; index < bytes.size();) {
    if (bytes[index] != '\x1b') {
      output.push_back(bytes[index++]);
      continue;
    }

    ++index;
    if (index < bytes.size() && bytes[index] == '[') {
      ++index;
      while (index < bytes.size()) {
        const unsigned char ch = static_cast<unsigned char>(bytes[index++]);
        if (ch >= 0x40 && ch <= 0x7e) {
          break;
        }
      }
      continue;
    }
    if (index < bytes.size()) {
      ++index;
    }
  }
  return output;
}

std::string FilterPlainLogText(const std::string& bytes) {
  std::string output;
  for (unsigned char ch : bytes) {
    if (ch == '\r' || ch == '\n' || ch == '\t' || (ch >= 0x20 && ch != 0x7f)) {
      output.push_back(static_cast<char>(ch));
    }
  }
  return output;
}

std::string CurrentLogTimestamp(bool utc) {
  std::string output;
  int result_code = 0;
  const std::string timezone = "GMT";
  const std::string* timezone_arg = utc ? &timezone : nullptr;
  if (!FormatLocalDateTime("%Y-%m-%d %H:%M:%S", timezone_arg, &output, &result_code)) {
    return "";
  }
  return "[" + output + "]\n";
}

bool ParseLogRotateSize(const std::string& value, std::uintmax_t* size) {
  std::string text = Trim(value);
  if (text.empty()) {
    return false;
  }

  std::uintmax_t multiplier = 1;
  const char suffix = static_cast<char>(std::toupper(static_cast<unsigned char>(text.back())));
  if (suffix == 'K' || suffix == 'M') {
    multiplier = suffix == 'K' ? 1024 : 1024 * 1024;
    text.pop_back();
  }
  if (text.empty() ||
      !std::all_of(text.begin(), text.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
    return false;
  }

  const std::uintmax_t parsed = static_cast<std::uintmax_t>(std::strtoull(text.c_str(), nullptr, 10));
  const std::uintmax_t bytes = parsed * multiplier;
  if (bytes < 128) {
    return false;
  }
  *size = bytes;
  return true;
}

std::string FileTimeToString(const std::filesystem::file_time_type& value) {
  using namespace std::chrono;
  const auto system_time = time_point_cast<system_clock::duration>(
      value - std::filesystem::file_time_type::clock::now() + system_clock::now());
  const std::time_t time = system_clock::to_time_t(system_time);
  std::tm local_time = {};
#if defined(_WIN32)
  localtime_s(&local_time, &time);
#else
  localtime_r(&time, &local_time);
#endif
  char buffer[32] = {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local_time);
  return buffer;
}

bool WildcardMatchInsensitive(std::string pattern, std::string name) {
  pattern = Lower(pattern == "*.*" ? "*" : pattern);
  name = Lower(std::move(name));

  std::size_t pattern_index = 0;
  std::size_t name_index = 0;
  std::size_t star_index = std::string::npos;
  std::size_t star_name_index = 0;
  while (name_index < name.size()) {
    if (pattern_index < pattern.size() &&
        (pattern[pattern_index] == '?' || pattern[pattern_index] == name[name_index])) {
      ++pattern_index;
      ++name_index;
      continue;
    }
    if (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
      star_index = pattern_index++;
      star_name_index = name_index;
      continue;
    }
    if (star_index != std::string::npos) {
      pattern_index = star_index + 1;
      name_index = ++star_name_index;
      continue;
    }
    return false;
  }

  while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
    ++pattern_index;
  }
  return pattern_index == pattern.size();
}

std::string DecodeSpecialDialogText(const std::string& value) {
  std::string result;
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] != '\\' || index + 1 >= value.size()) {
      result.push_back(value[index]);
      continue;
    }
    const char next = value[++index];
    if (next == 'n') {
      result.push_back('\n');
    } else if (next == 't') {
      result.push_back('\t');
    } else {
      result.push_back(next);
    }
  }
  return result;
}

constexpr std::size_t kMaxCallDepth = 128;

}  // namespace

MacroRunner::MacroRunner(const std::string& script, MacroOptions options)
    : lines_(SplitLines(script)),
      options_(std::move(options)),
      working_directory_(CurrentDirectoryString()),
      transfer_directory_(working_directory_),
      tera_term_directory_(working_directory_) {
  SetInteger("result", 0);
  SetInteger("timeout", 0);
  SetInteger("mtimeout", 0);
  SetString("inputstr", "");
  SetString("matchstr", "");
  ClearGroupMatchStrings();
  BuildLabels();
}

MacroRunner::~MacroRunner() {
  for (auto& entry : files_) {
    ReleaseFileLock(&entry.second);
  }
}

bool MacroRunner::RespondDialog(bool accepted, const std::string& value) {
  if (wait_.kind != WaitKind::Dialog) {
    return false;
  }
  wait_.dialog_answer_ready = true;
  wait_.dialog_accepted = accepted;
  wait_.dialog_value = value;
  return true;
}

void MacroRunner::BuildLabels() {
  for (std::size_t index = 0; index < lines_.size(); ++index) {
    const std::string line = Trim(StripComment(lines_[index]));
    if (line.empty() || line.front() != ':') {
      continue;
    }
    std::size_t end = 1;
    while (end < line.size() && IsIdentifierChar(line[end])) {
      ++end;
    }
    const std::string label = NormalizeName(line.substr(1, end - 1));
    if (!label.empty()) {
      labels_[label] = index + 1;
    }
  }
}

MacroStepResult MacroRunner::Advance(TerminalSession* session, std::size_t max_commands) {
  MacroStepResult result;
  if (done_) {
    SetCompleted(&result);
    AttachPendingUpdates(&result);
    return result;
  }
  if (session == nullptr) {
    SetError(&result, "macro session is missing");
    AttachPendingUpdates(&result);
    return result;
  }

  FlushLogOutput(session);
  result = CheckWait(session);
  if (result.status == MacroStatus::Waiting || result.status == MacroStatus::Error) {
    FlushLogOutput(session);
    CloseAutoLogIfDone(session);
    AttachPendingUpdates(&result);
    return result;
  }

  std::size_t commands = 0;
  while (!done_ && next_line_ < lines_.size() && commands < max_commands) {
    const std::string line = Trim(StripComment(lines_[next_line_]));
    ++next_line_;
    if (line.empty() || line.front() == ':') {
      continue;
    }
    ++commands;
    result = ExecuteLine(session, line);
    if (result.status != MacroStatus::Running) {
      FlushLogOutput(session);
      CloseAutoLogIfDone(session);
      result.next_line = next_line_;
      AttachPendingUpdates(&result);
      return result;
    }
  }

  if (next_line_ >= lines_.size()) {
    if (!call_stack_.empty()) {
      SetError(&result, "subroutine reached end without return");
    } else {
      SetCompleted(&result);
    }
  } else {
    result.status = MacroStatus::Running;
    result.message = "macro yielded";
  }
  FlushLogOutput(session);
  CloseAutoLogIfDone(session);
  result.next_line = next_line_;
  AttachPendingUpdates(&result);
  return result;
}

MacroStepResult MacroRunner::ExecuteLine(TerminalSession* session, const std::string& line) {
  MacroStepResult result;
  const std::vector<std::string> tokens = Tokenize(line);
  if (tokens.empty()) {
    return result;
  }

  const std::string command = Lower(tokens[0]);
  if (command == "end") {
    SetCompleted(&result, "macro ended");
  } else if (command == "disconnect") {
    session->Disconnect();
  } else if (command == "connect") {
    if (tokens.size() < 2) {
      SetError(&result, "connect requires a host specification");
    } else {
      result = Connect(session, JoinEvaluatedArguments(tokens, 1, " "));
    }
  } else if (command == "goto") {
    if (tokens.size() < 2) {
      SetError(&result, "goto requires a label");
    } else {
      std::string error;
      if (!JumpToLabel(tokens[1], &error)) {
        SetError(&result, error);
      }
    }
  } else if (command == "call") {
    result = ExecuteCall(tokens);
  } else if (command == "return") {
    result = ExecuteReturn(tokens);
  } else if (command == "if") {
    result = ExecuteIf(session, line);
  } else if (command == "elseif" || command == "else") {
    std::string error;
    if (!SkipToEndIf(&error)) {
      SetError(&result, error);
    }
  } else if (command == "endif") {
    result.status = MacroStatus::Running;
  } else if (command == "while") {
    result = ExecuteWhile(tokens);
  } else if (command == "endwhile") {
    std::string error;
    if (!JumpBackToWhile(&error)) {
      SetError(&result, error);
    }
  } else if (command == "until") {
    result = ExecuteUntil(tokens);
  } else if (command == "enduntil") {
    std::string error;
    if (!JumpBackToUntil(&error)) {
      SetError(&result, error);
    }
  } else if (command == "do") {
    result = ExecuteDo(tokens);
  } else if (command == "loop") {
    result = ExecuteLoop(tokens);
  } else if (command == "for") {
    result = ExecuteFor(tokens);
  } else if (command == "next") {
    result = ExecuteNext(tokens);
  } else if (command == "break") {
    result = ExecuteBreak(false);
  } else if (command == "continue") {
    result = ExecuteBreak(true);
  } else if (command == "intdim") {
    result = ExecuteDim(tokens, false);
  } else if (command == "strdim") {
    result = ExecuteDim(tokens, true);
  } else if (command == "strcompare") {
    result = ExecuteStrCompare(tokens);
  } else if (command == "strscan") {
    result = ExecuteStrScan(tokens);
  } else if (command == "strcopy") {
    result = ExecuteStrCopy(tokens);
  } else if (command == "strmatch") {
    result = ExecuteStrMatch(tokens);
  } else if (command == "strreplace") {
    result = ExecuteStrReplace(tokens);
  } else if (command == "sprintf") {
    result = ExecuteSprintf(tokens, false);
  } else if (command == "sprintf2") {
    result = ExecuteSprintf(tokens, true);
  } else if (command == "send" || command == "sendln") {
    std::string text = JoinEvaluatedArguments(tokens, 1, "");
    if (command == "sendln") {
      text += "\r";
    }
    std::string error;
    if (!session->SendInput(text, &error)) {
      SetError(&result, error);
    } else {
      SetInteger("result", 1);
    }
  } else if (command == "wait" || command == "waitln" || command == "waitregex") {
    if (tokens.size() < 2) {
      SetError(&result, command + " requires at least one text argument");
      result.next_line = next_line_;
      return result;
    }
    wait_.kind = WaitKind::Text;
    wait_.regex = command == "waitregex";
    wait_.deadline_set = false;
    wait_.texts.clear();
    for (std::size_t index = 1; index < tokens.size(); ++index) {
      wait_.texts.push_back(EvaluateToken(tokens[index]));
    }
    if (command == "waitln") {
      for (std::string& text : wait_.texts) {
        text += "\r\n";
      }
    }
    result = CheckWait(session);
  } else if (command == "messagebox" || command == "yesnobox" ||
             command == "inputbox" || command == "passwordbox") {
    result = ExecuteDialog(tokens);
  } else if (command == "filenamebox") {
    result = ExecuteFileNameBox(tokens, false);
  } else if (command == "dirnamebox") {
    result = ExecuteFileNameBox(tokens, true);
  } else if (command == "listbox") {
    result = ExecuteListBox(tokens);
  } else if (command == "statusbox") {
    result = ExecuteStatusBox(tokens, false);
  } else if (command == "closesbox") {
    result = ExecuteStatusBox(tokens, true);
  } else if (command == "pause" || command == "mpause") {
    const int amount = tokens.size() > 1 ? std::atoi(tokens[1].c_str()) : 0;
    const int wait_ms = command == "mpause" ? amount : amount * 1000;
    wait_.kind = WaitKind::Pause;
    wait_.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(wait_ms, 0));
    result = CheckWait(session);
  } else if (command == "setsync") {
    SetInteger("result", 0);
  } else if (command == "strconcat") {
    std::string array_name;
    std::string index_expression;
    if (tokens.size() < 3 ||
        (!IsIdentifier(tokens[1]) && !ParseArrayReference(tokens[1], &array_name, &index_expression))) {
      SetError(&result, "strconcat requires a string variable and text");
    } else {
      MacroValue current_value;
      const auto iter = variables_.find(NormalizeName(tokens[1]));
      const bool has_array_value = ResolveArrayValue(tokens[1], &current_value);
      const std::string current = has_array_value ? (current_value.is_string ? current_value.string_value :
                                                    std::to_string(current_value.int_value)) :
                                  (iter == variables_.end() ? "" :
                                   (iter->second.is_string ? iter->second.string_value :
                                    std::to_string(iter->second.int_value)));
      MacroValue value;
      value.is_string = true;
      value.string_value = current + JoinEvaluatedArguments(tokens, 2, "");
      std::string error;
      if (!SetValue(tokens[1], value, &error)) {
        SetError(&result, error);
      } else {
        SetInteger("result", 0);
      }
    }
  } else if (command == "strlen") {
    std::string array_name;
    std::string index_expression;
    if (tokens.size() < 3 ||
        (!IsIdentifier(tokens[1]) && !ParseArrayReference(tokens[1], &array_name, &index_expression))) {
      SetError(&result, "strlen requires an integer variable and text");
    } else {
      MacroValue value;
      value.int_value = static_cast<int>(JoinEvaluatedArguments(tokens, 2, "").size());
      std::string error;
      if (!SetValue(tokens[1], value, &error)) {
        SetError(&result, error);
      } else {
        SetInteger("result", 0);
      }
    }
  } else if (command == "str2int") {
    std::string array_name;
    std::string index_expression;
    if (tokens.size() < 3 ||
        (!IsIdentifier(tokens[1]) && !ParseArrayReference(tokens[1], &array_name, &index_expression))) {
      SetError(&result, "str2int requires an integer variable and text");
    } else {
      MacroValue value;
      value.int_value = EvaluateInteger(tokens[2]);
      std::string error;
      if (!SetValue(tokens[1], value, &error)) {
        SetError(&result, error);
      } else {
        SetInteger("result", 0);
      }
    }
  } else if (command == "int2str") {
    std::string array_name;
    std::string index_expression;
    if (tokens.size() < 3 ||
        (!IsIdentifier(tokens[1]) && !ParseArrayReference(tokens[1], &array_name, &index_expression))) {
      SetError(&result, "int2str requires a string variable and integer");
    } else {
      MacroValue value;
      value.is_string = true;
      value.string_value = std::to_string(EvaluateInteger(tokens[2]));
      std::string error;
      if (!SetValue(tokens[1], value, &error)) {
        SetError(&result, error);
      } else {
        SetInteger("result", 0);
      }
    }
  } else if (command == "getdir") {
    result = ExecuteGetDir(tokens, false);
  } else if (command == "getttdir") {
    result = ExecuteGetDir(tokens, true);
  } else if (command == "setdir") {
    result = ExecuteSetDir(tokens, false);
  } else if (command == "changedir") {
    result = ExecuteSetDir(tokens, true);
  } else if (command == "getenv") {
    result = ExecuteGetEnv(tokens);
  } else if (command == "expandenv") {
    result = ExecuteExpandEnv(tokens);
  } else if (command == "getdate") {
    result = ExecuteGetDateTime(tokens, true);
  } else if (command == "gettime") {
    result = ExecuteGetDateTime(tokens, false);
  } else if (command == "uptime") {
    result = ExecuteUptime(tokens);
  } else if (command == "logopen") {
    result = ExecuteLogOpen(session, tokens);
  } else if (command == "logclose") {
    result = ExecuteLogClose(session, tokens);
  } else if (command == "logwrite") {
    result = ExecuteLogWrite(tokens);
  } else if (command == "logpause") {
    result = ExecuteLogControl(session, tokens, true);
  } else if (command == "logstart") {
    result = ExecuteLogControl(session, tokens, false);
  } else if (command == "loginfo") {
    result = ExecuteLogInfo(tokens);
  } else if (command == "logrotate") {
    result = ExecuteLogRotate(tokens);
  } else if (command == "logautoclosemode") {
    result = ExecuteLogAutoCloseMode(tokens);
  } else if (command == "fileconcat") {
    result = ExecuteFileCopy(tokens, true);
  } else if (command == "filecopy") {
    result = ExecuteFileCopy(tokens, false);
  } else if (command == "filecreate") {
    result = ExecuteFileCreate(tokens);
  } else if (command == "filedelete") {
    result = ExecuteFileDelete(tokens);
  } else if (command == "fileopen") {
    result = ExecuteFileOpen(tokens);
  } else if (command == "fileread") {
    result = ExecuteFileRead(tokens, false);
  } else if (command == "filereadln") {
    result = ExecuteFileRead(tokens, true);
  } else if (command == "filerename") {
    result = ExecuteFileRename(tokens);
  } else if (command == "filesearch") {
    result = ExecuteFileSearch(tokens, false);
  } else if (command == "fileseek") {
    result = ExecuteFileSeek(tokens);
  } else if (command == "filemarkptr") {
    result = ExecuteFileMarkPtr(tokens);
  } else if (command == "filelock") {
    result = ExecuteFileLock(tokens, false);
  } else if (command == "fileseekback") {
    result = ExecuteFileSeek(tokens);
  } else if (command == "filestat") {
    result = ExecuteFileStat(tokens);
  } else if (command == "filestrseek") {
    result = ExecuteFileStrSeek(tokens, false);
  } else if (command == "filestrseek2") {
    result = ExecuteFileStrSeek(tokens, true);
  } else if (command == "filetruncate") {
    result = ExecuteFileTruncate(tokens);
  } else if (command == "filewrite") {
    result = ExecuteFileWrite(tokens, false);
  } else if (command == "filewriteln") {
    result = ExecuteFileWrite(tokens, true);
  } else if (command == "fileclose") {
    result = ExecuteFileClose(tokens);
  } else if (command == "fileunlock") {
    result = ExecuteFileLock(tokens, true);
  } else if (command == "foldercreate") {
    result = ExecuteFolderCreate(tokens);
  } else if (command == "folderdelete") {
    result = ExecuteFolderDelete(tokens);
  } else if (command == "foldersearch") {
    result = ExecuteFileSearch(tokens, true);
  } else if (command == "findfirst") {
    result = ExecuteFindFirst(tokens);
  } else if (command == "findnext") {
    result = ExecuteFindNext(tokens);
  } else if (command == "findclose") {
    result = ExecuteFindClose(tokens);
  } else {
    std::string error;
    if (AssignLine(line, &error)) {
      SetInteger("result", 0);
    } else if (!error.empty()) {
      SetError(&result, error);
    } else {
      SetError(&result, "unsupported macro command: " + tokens[0]);
    }
  }

  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::CheckWait(TerminalSession* session) {
  MacroStepResult result;
  if (wait_.kind == WaitKind::None) {
    result.status = MacroStatus::Running;
    return result;
  }

  if (wait_.kind == WaitKind::Text) {
    const std::string& history = session->OutputHistory();
    for (std::size_t index = 0; index < wait_.texts.size(); ++index) {
      bool matched = false;
      std::string match_text = wait_.texts[index];
      if (wait_.regex) {
        try {
          std::smatch match;
          matched = std::regex_search(history, match, std::regex(wait_.texts[index]));
          if (matched && !match.empty()) {
            match_text = match.str(0);
            SetRegexMatchVariables(match);
          }
        } catch (const std::regex_error&) {
          SetError(&result, "waitregex pattern is invalid");
          return result;
        }
      } else {
        matched = wait_.texts[index].empty() ? !history.empty() :
                  history.find(wait_.texts[index]) != std::string::npos;
      }
      if (matched) {
        SetInteger("result", static_cast<int>(index + 1));
        if (!wait_.regex) {
          SetString("matchstr", match_text);
        }
        wait_ = WaitState{};
        result.status = MacroStatus::Running;
        return result;
      }
    }

    const int timeout_ms = WaitTimeoutMs();
    if (timeout_ms > 0 && !wait_.deadline_set) {
      wait_.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
      wait_.deadline_set = true;
    }
    if (wait_.deadline_set && std::chrono::steady_clock::now() >= wait_.deadline) {
      SetInteger("result", 0);
      SetString("matchstr", "");
      wait_ = WaitState{};
      result.status = MacroStatus::Running;
      return result;
    }
    SetWaiting(&result, "waiting for text", 100);
    return result;
  }

  if (wait_.kind == WaitKind::Dialog) {
    if (!wait_.dialog_answer_ready) {
      SetWaiting(&result, "macro dialog: " + wait_.dialog_title, 0);
      AttachDialog(&result);
      return result;
    }

    const std::string dialog_type = wait_.dialog_type;
    const bool accepted = wait_.dialog_accepted;
    const std::string value = wait_.dialog_value;
    wait_ = WaitState{};
    if (dialog_type == "yesnobox") {
      SetInteger("result", accepted ? 1 : 0);
    } else if (dialog_type == "inputbox" || dialog_type == "passwordbox") {
      SetString("inputstr", accepted ? value : "");
      SetInteger("result", accepted ? 1 : 0);
    } else if (dialog_type == "filenamebox" || dialog_type == "dirnamebox") {
      SetString("inputstr", accepted ? value : "");
      SetInteger("result", accepted && !value.empty() ? 1 : 0);
    } else if (dialog_type == "listbox") {
      SetInteger("result", accepted ? std::atoi(value.c_str()) : -1);
    } else {
      SetInteger("result", 0);
    }
    result.status = MacroStatus::Running;
    return result;
  }

  const auto now = std::chrono::steady_clock::now();
  if (now >= wait_.deadline) {
    wait_ = WaitState{};
    result.status = MacroStatus::Running;
    return result;
  }

  const auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(wait_.deadline - now).count();
  SetWaiting(&result, "pause", static_cast<int>(wait_ms));
  return result;
}

MacroStepResult MacroRunner::ExecuteIf(TerminalSession* session, const std::string& line) {
  MacroStepResult result;
  const std::vector<std::string> tokens = Tokenize(line);
  const std::size_t then_index = ThenTokenIndex(tokens);
  if (then_index != std::string::npos && then_index + 1 >= tokens.size()) {
    if (then_index <= 1) {
      SetError(&result, "if requires an expression before then");
      return result;
    }
    const std::string expression = JoinTokens(tokens, 1, then_index);
    if (!EvaluateCondition(expression)) {
      std::string error;
      if (!SkipToConditionalBranch(&error)) {
        SetError(&result, error);
      }
    }
    result.next_line = next_line_;
    return result;
  }

  std::size_t statement_index = 0;
  std::size_t expression_end = 0;
  for (std::size_t index = 1; index < tokens.size(); ++index) {
    const std::string token = Lower(tokens[index]);
    if (token == "then") {
      if (index + 1 >= tokens.size()) {
        SetError(&result, "if then requires a statement or a block body");
        return result;
      }
      statement_index = index + 1;
      expression_end = index;
      break;
    }
    if (IsStatementKeyword(token) || (index > 1 && LooksLikeAssignmentStatement(tokens[index]))) {
      statement_index = index;
      expression_end = index;
      break;
    }
  }

  if (statement_index == 0 || expression_end <= 1) {
    SetError(&result, "if requires an expression and a one-line statement");
    return result;
  }

  std::string normalized_expression;
  for (std::size_t index = 1; index < expression_end; ++index) {
    if (!normalized_expression.empty()) {
      normalized_expression += " ";
    }
    normalized_expression += tokens[index];
  }

  if (EvaluateCondition(normalized_expression)) {
    result = ExecuteLine(session, JoinArguments(tokens, statement_index, " "));
  }
  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::ExecuteWhile(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() < 2) {
    SetError(&result, "while requires an expression");
    return result;
  }

  if (!EvaluateCondition(JoinTokens(tokens, 1, tokens.size()))) {
    std::string error;
    if (!SkipToEndWhile(&error)) {
      SetError(&result, error);
    }
  }
  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::ExecuteUntil(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() < 2) {
    SetError(&result, "until requires an expression");
    return result;
  }

  if (EvaluateCondition(JoinTokens(tokens, 1, tokens.size()))) {
    std::string error;
    if (!SkipToEndUntil(&error)) {
      SetError(&result, error);
    }
  }
  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::ExecuteDo(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() == 1) {
    return result;
  }
  if (tokens.size() < 3) {
    SetError(&result, "do requires while/until and an expression");
    return result;
  }

  const std::string mode = Lower(tokens[1]);
  bool should_enter = false;
  if (mode == "while") {
    should_enter = EvaluateCondition(JoinTokens(tokens, 2, tokens.size()));
  } else if (mode == "until") {
    should_enter = !EvaluateCondition(JoinTokens(tokens, 2, tokens.size()));
  } else {
    SetError(&result, "do requires while or until");
    return result;
  }

  if (!should_enter) {
    std::string error;
    if (!SkipToLoop(&error)) {
      SetError(&result, error);
    }
  }
  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::ExecuteLoop(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  bool repeat = true;
  if (tokens.size() != 1) {
    if (tokens.size() < 3) {
      SetError(&result, "loop requires while/until and an expression");
      return result;
    }

    const std::string mode = Lower(tokens[1]);
    if (mode == "while") {
      repeat = EvaluateCondition(JoinTokens(tokens, 2, tokens.size()));
    } else if (mode == "until") {
      repeat = !EvaluateCondition(JoinTokens(tokens, 2, tokens.size()));
    } else {
      SetError(&result, "loop requires while or until");
      return result;
    }
  }

  if (repeat) {
    std::string error;
    if (!JumpBackToDo(&error)) {
      SetError(&result, error);
    }
  }
  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::ExecuteFor(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 4 || !IsIdentifier(tokens[1])) {
    SetError(&result, "for requires an integer variable, first value, and last value");
    return result;
  }

  const std::size_t line_index = next_line_ == 0 ? 0 : next_line_ - 1;
  const int first = EvaluateInteger(tokens[2]);
  const int last = EvaluateInteger(tokens[3]);
  SetInteger(tokens[1], first);
  for_stack_.push_back(ForFrame{line_index, NormalizeName(tokens[1]), last});
  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::ExecuteNext(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 1) {
    SetError(&result, "next does not accept arguments");
    return result;
  }
  if (for_stack_.empty()) {
    SetError(&result, "next is missing for");
    return result;
  }

  ForFrame& frame = for_stack_.back();
  int current = EvaluateInteger(frame.variable);
  if (current == frame.last) {
    for_stack_.pop_back();
    result.next_line = next_line_;
    return result;
  }

  if (current < frame.last) {
    ++current;
  } else {
    --current;
  }
  SetInteger(frame.variable, current);
  next_line_ = frame.line_index + 1;
  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::ExecuteBreak(bool continue_loop) {
  MacroStepResult result;
  std::string boundary;
  std::string error;
  if (!MoveToNearestLoopBoundary(!continue_loop, &boundary, &error)) {
    SetError(&result, error);
    return result;
  }

  if (!continue_loop && boundary == "next") {
    if (for_stack_.empty()) {
      SetError(&result, "break found next without active for");
      return result;
    }
    for_stack_.pop_back();
  }

  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::ExecuteDim(const std::vector<std::string>& tokens, bool string_array) {
  MacroStepResult result;
  if (tokens.size() != 3 || !IsIdentifier(tokens[1])) {
    SetError(&result, string_array ? "strdim requires an array name and size" :
                                     "intdim requires an array name and size");
    return result;
  }

  const int size = EvaluateInteger(tokens[2]);
  if (size < 1 || size > 65536) {
    SetError(&result, "array size must be between 1 and 65536");
    return result;
  }

  const std::string name = NormalizeName(tokens[1]);
  if (string_array) {
    string_arrays_[name] = std::vector<std::string>(static_cast<std::size_t>(size));
    int_arrays_.erase(name);
  } else {
    int_arrays_[name] = std::vector<int>(static_cast<std::size_t>(size), 0);
    string_arrays_.erase(name);
  }
  variables_.erase(name);
  SetInteger("result", 0);
  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::ExecuteStrCompare(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 3) {
    SetError(&result, "strcompare requires two strings");
    return result;
  }

  const std::string left = EvaluateToken(tokens[1]);
  const std::string right = EvaluateToken(tokens[2]);
  SetInteger("result", left < right ? -1 : (left > right ? 1 : 0));
  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::ExecuteStrScan(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 3) {
    SetError(&result, "strscan requires a string and substring");
    return result;
  }

  const std::string text = EvaluateToken(tokens[1]);
  const std::string needle = EvaluateToken(tokens[2]);
  const std::size_t found = needle.empty() ? std::string::npos : text.find(needle);
  SetInteger("result", found == std::string::npos ? 0 : static_cast<int>(found + 1));
  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::ExecuteStrCopy(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 5) {
    SetError(&result, "strcopy requires a string, position, length, and string variable");
    return result;
  }

  const std::string source = EvaluateToken(tokens[1]);
  const int position = EvaluateInteger(tokens[2]);
  const int length = EvaluateInteger(tokens[3]);
  const std::size_t start = position <= 1 ? 0 : static_cast<std::size_t>(position - 1);
  const std::string copied = start >= source.size() || length <= 0 ? "" :
                             source.substr(start, static_cast<std::size_t>(length));

  MacroValue value;
  value.is_string = true;
  value.string_value = copied;
  std::string error;
  if (!SetValue(tokens[4], value, &error)) {
    SetError(&result, error);
  } else {
    SetInteger("result", 0);
  }
  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::ExecuteStrMatch(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 3) {
    SetError(&result, "strmatch requires a string and regular expression");
    return result;
  }

  const std::string text = EvaluateToken(tokens[1]);
  const std::string pattern = EvaluateToken(tokens[2]);
  try {
    std::smatch match;
    if (std::regex_search(text, match, std::regex(pattern))) {
      SetInteger("result", static_cast<int>(match.position(0) + 1));
      SetRegexMatchVariables(match);
    } else {
      SetInteger("result", 0);
    }
  } catch (const std::regex_error&) {
    SetInteger("result", 0);
  }

  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::ExecuteStrReplace(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  std::string array_name;
  std::string index_expression;
  if (tokens.size() != 5 ||
      (!IsIdentifier(tokens[1]) && !ParseArrayReference(tokens[1], &array_name, &index_expression))) {
    SetError(&result, "strreplace requires a string variable, position, regular expression, and replacement");
    return result;
  }

  MacroValue current_value;
  std::string current;
  if (ResolveArrayValue(tokens[1], &current_value)) {
    current = current_value.is_string ? current_value.string_value : std::to_string(current_value.int_value);
  } else {
    const auto iter = variables_.find(NormalizeName(tokens[1]));
    current = iter == variables_.end() ? "" :
              (iter->second.is_string ? iter->second.string_value : std::to_string(iter->second.int_value));
  }

  const int position = EvaluateInteger(tokens[2]);
  if (position <= 0 || static_cast<std::size_t>(position) > current.size()) {
    SetInteger("result", 0);
    result.next_line = next_line_;
    return result;
  }

  const std::size_t start = static_cast<std::size_t>(position - 1);
  const std::string tail = current.substr(start);
  const std::string pattern = EvaluateToken(tokens[3]);
  const std::string replacement = EvaluateToken(tokens[4]);

  try {
    std::smatch match;
    if (!std::regex_search(tail, match, std::regex(pattern))) {
      SetInteger("result", 0);
      result.next_line = next_line_;
      return result;
    }

    const std::size_t match_begin = start + static_cast<std::size_t>(match.position(0));
    const std::size_t match_end = match_begin + static_cast<std::size_t>(match.length(0));
    MacroValue value;
    value.is_string = true;
    value.string_value = current.substr(0, match_begin) + replacement + current.substr(match_end);

    std::string error;
    if (!SetValue(tokens[1], value, &error)) {
      SetError(&result, error);
      return result;
    }

    SetRegexMatchVariables(match);
    SetInteger("result", 1);
  } catch (const std::regex_error&) {
    SetInteger("result", -1);
  }

  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::ExecuteSprintf(const std::vector<std::string>& tokens, bool explicit_destination) {
  MacroStepResult result;
  const std::size_t format_index = explicit_destination ? 2 : 1;
  if (tokens.size() <= format_index) {
    SetString("inputstr", "");
    SetInteger("result", 1);
    result.next_line = next_line_;
    return result;
  }

  std::string formatted;
  int result_code = 0;
  if (!FormatString(EvaluateToken(tokens[format_index]), tokens, format_index + 1, &formatted, &result_code)) {
    SetInteger("result", result_code);
    result.next_line = next_line_;
    return result;
  }

  MacroValue value;
  value.is_string = true;
  value.string_value = formatted;
  std::string error;
  if (explicit_destination) {
    if (!SetValue(tokens[1], value, &error)) {
      SetInteger("result", 4);
      result.next_line = next_line_;
      return result;
    }
  } else {
    SetString("inputstr", formatted);
  }

  SetInteger("result", 0);
  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::ExecuteCall(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 2) {
    SetError(&result, "call requires a label");
    return result;
  }
  if (call_stack_.size() >= kMaxCallDepth) {
    SetError(&result, "macro call stack overflow");
    return result;
  }

  const std::size_t return_line = next_line_;
  std::string error;
  if (!JumpToLabel(tokens[1], &error)) {
    SetError(&result, error);
    return result;
  }

  call_stack_.push_back(return_line);
  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::ExecuteReturn(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() > 1) {
    SetError(&result, "return does not accept arguments");
    return result;
  }
  if (call_stack_.empty()) {
    SetError(&result, "return requires an active subroutine call");
    return result;
  }

  next_line_ = call_stack_.back();
  call_stack_.pop_back();
  result.next_line = next_line_;
  return result;
}

MacroStepResult MacroRunner::ExecuteFileCopy(const std::vector<std::string>& tokens, bool concat) {
  MacroStepResult result;
  if (tokens.size() != 3) {
    SetError(&result, concat ? "fileconcat requires two filenames" : "filecopy requires two filenames");
    return result;
  }

  const std::filesystem::path source = ResolvePath(EvaluateToken(tokens[concat ? 2 : 1]));
  const std::filesystem::path target = ResolvePath(EvaluateToken(tokens[concat ? 1 : 2]));
  if (target == source) {
    SetInteger("result", concat ? 2 : -3);
    return result;
  }

  if (concat) {
    std::ofstream output(target, std::ios::binary | std::ios::app);
    if (!output.is_open()) {
      SetInteger("result", 3);
      return result;
    }
    std::ifstream input(source, std::ios::binary);
    if (!input.is_open()) {
      SetInteger("result", -4);
      return result;
    }
    output << input.rdbuf();
    SetInteger("result", output.good() && !input.bad() ? 0 : -5);
    return result;
  }

  std::error_code error;
  std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, error);
  SetInteger("result", error ? -4 : 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteFileCreate(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 3 || !IsIdentifier(tokens[1])) {
    SetError(&result, "filecreate requires a handle variable and filename");
    return result;
  }

  const std::string filename = ResolvePath(EvaluateToken(tokens[2])).string();
  auto stream = std::make_unique<std::fstream>();
  stream->open(filename, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
  if (!stream->is_open()) {
    SetInteger(tokens[1], -1);
    SetInteger("result", 2);
    return result;
  }

  const int handle = next_file_handle_++;
  files_[handle] = FileHandle{std::move(stream), 0, filename, -1, false};
  SetInteger(tokens[1], handle);
  SetInteger("result", 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteFileDelete(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 2) {
    SetError(&result, "filedelete requires a filename");
    return result;
  }

  std::error_code error;
  const bool removed = std::filesystem::remove(ResolvePath(EvaluateToken(tokens[1])), error);
  SetInteger("result", removed && !error ? 0 : -1);
  return result;
}

MacroStepResult MacroRunner::ExecuteFileOpen(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() < 4 || !IsIdentifier(tokens[1])) {
    SetError(&result, "fileopen requires a handle variable, filename, and append flag");
    return result;
  }

  const std::string filename = ResolvePath(EvaluateToken(tokens[2])).string();
  const bool append = EvaluateInteger(tokens[3]) != 0;
  const bool readonly = tokens.size() > 4 && EvaluateInteger(tokens[4]) != 0;

  auto stream = std::make_unique<std::fstream>();
  std::ios::openmode mode = std::ios::binary | std::ios::in;
  if (!readonly) {
    mode |= std::ios::out;
  }
  stream->open(filename, mode);
  if (!stream->is_open() && !readonly) {
    std::ofstream create_file(filename, std::ios::binary | std::ios::app);
    create_file.close();
    stream->open(filename, mode);
  }

  if (!stream->is_open()) {
    SetInteger(tokens[1], -1);
    SetInteger("result", -1);
    return result;
  }

  if (append) {
    stream->seekg(0, std::ios::end);
    stream->seekp(0, std::ios::end);
  } else {
    stream->seekg(0, std::ios::beg);
    stream->seekp(0, std::ios::beg);
  }

  const int handle = next_file_handle_++;
  files_[handle] = FileHandle{std::move(stream), 0, filename, -1, false};
  SetInteger(tokens[1], handle);
  SetInteger("result", 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteFileRead(const std::vector<std::string>& tokens, bool line_mode) {
  MacroStepResult result;
  const std::size_t min_args = line_mode ? 3 : 4;
  if (tokens.size() < min_args || !IsIdentifier(tokens[line_mode ? 2 : 3])) {
    SetError(&result, line_mode ? "filereadln requires a handle and string variable" :
                                  "fileread requires a handle, byte count, and string variable");
    return result;
  }

  const int handle = EvaluateInteger(tokens[1]);
  auto iter = files_.find(handle);
  if (iter == files_.end() || !iter->second.stream || !iter->second.stream->is_open()) {
    SetError(&result, "file handle is not open");
    return result;
  }

  if (line_mode) {
    iter->second.stream->clear();
    const std::streampos start = iter->second.stream->tellg();
    std::string line;
    if (std::getline(*iter->second.stream, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      SetString(tokens[2], line);
      SetInteger("result", 0);
    } else {
      SetString(tokens[2], "");
      SetInteger("result", 1);
    }
    std::streampos next = iter->second.stream->tellg();
    if (next == std::streampos(-1) && start != std::streampos(-1)) {
      iter->second.stream->clear();
      iter->second.stream->seekg(0, std::ios::end);
      next = iter->second.stream->tellg();
    }
    if (next != std::streampos(-1)) {
      iter->second.stream->clear();
      iter->second.stream->seekp(next);
    }
    return result;
  }

  const int requested = std::max(EvaluateInteger(tokens[2]), 0);
  iter->second.stream->clear();
  const std::streampos start = iter->second.stream->tellg();
  std::string bytes(static_cast<std::size_t>(requested), '\0');
  iter->second.stream->read(bytes.data(), bytes.size());
  const std::streamsize read_count = iter->second.stream->gcount();
  bytes.resize(static_cast<std::size_t>(read_count));
  if (start != std::streampos(-1)) {
    iter->second.stream->clear();
    iter->second.stream->seekp(start + static_cast<std::streamoff>(read_count));
  }
  SetString(tokens[3], bytes);
  SetInteger("result", bytes.size() < static_cast<std::size_t>(requested) ? 1 : 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteFileRename(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 3) {
    SetError(&result, "filerename requires two filenames");
    return result;
  }

  const std::filesystem::path source = ResolvePath(EvaluateToken(tokens[1]));
  const std::filesystem::path target = ResolvePath(EvaluateToken(tokens[2]));
  if (source == target) {
    SetInteger("result", 2);
    return result;
  }

  std::error_code error;
  std::filesystem::rename(source, target, error);
  SetInteger("result", error ? -3 : 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteFileSearch(const std::vector<std::string>& tokens, bool folder_only) {
  MacroStepResult result;
  if (tokens.size() != 2) {
    SetError(&result, folder_only ? "foldersearch requires a folder name" : "filesearch requires a filename");
    return result;
  }

  std::error_code error;
  const std::filesystem::path path = ResolvePath(EvaluateToken(tokens[1]));
  const bool found = folder_only ? std::filesystem::is_directory(path, error) :
                                   std::filesystem::exists(path, error);
  SetInteger("result", !error && found ? 1 : 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteFileSeek(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  const std::string command = Lower(tokens[0]);
  const bool seek_back = command == "fileseekback";
  if ((!seek_back && tokens.size() != 4) || (seek_back && tokens.size() != 2)) {
    SetError(&result, seek_back ? "fileseekback requires a handle" :
                                  "fileseek requires a handle, offset, and origin");
    return result;
  }

  const int handle = EvaluateInteger(tokens[1]);
  auto iter = files_.find(handle);
  if (iter == files_.end() || !iter->second.stream || !iter->second.stream->is_open()) {
    SetError(&result, "file handle is not open");
    return result;
  }

  std::streamoff offset = 0;
  std::ios::seekdir origin = std::ios::beg;
  if (seek_back) {
    offset = iter->second.marked_position;
  } else {
    offset = static_cast<std::streamoff>(EvaluateInteger(tokens[2]));
    const int origin_value = EvaluateInteger(tokens[3]);
    origin = origin_value == 1 ? std::ios::cur : (origin_value == 2 ? std::ios::end : std::ios::beg);
  }

  iter->second.stream->clear();
  iter->second.stream->seekg(offset, origin);
  iter->second.stream->seekp(offset, origin);
  SetInteger("result", iter->second.stream->fail() ? -1 : 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteFileMarkPtr(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 2) {
    SetError(&result, "filemarkptr requires a handle");
    return result;
  }

  const int handle = EvaluateInteger(tokens[1]);
  auto iter = files_.find(handle);
  if (iter == files_.end() || !iter->second.stream || !iter->second.stream->is_open()) {
    SetError(&result, "file handle is not open");
    return result;
  }

  iter->second.stream->clear();
  std::streampos position = iter->second.stream->tellg();
  if (position == std::streampos(-1)) {
    position = iter->second.stream->tellp();
  }
  iter->second.marked_position = position == std::streampos(-1) ? std::streampos(0) : position;
  SetInteger("result", 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteFileStat(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() < 3 || tokens.size() > 5) {
    SetError(&result, "filestat requires a filename, size variable, and optional time/drive variables");
    return result;
  }

  const std::filesystem::path path = ResolvePath(EvaluateToken(tokens[1]));
  std::error_code error;
  if (!std::filesystem::exists(path, error) || error) {
    SetInteger("result", -1);
    return result;
  }

  const bool is_directory = std::filesystem::is_directory(path, error);
  const auto size = is_directory || error ? 0 : static_cast<int>(std::filesystem::file_size(path, error));
  if (error) {
    SetInteger("result", -1);
    return result;
  }

  MacroValue value;
  value.int_value = size;
  std::string set_error;
  if (!SetValue(tokens[2], value, &set_error)) {
    SetError(&result, set_error);
    return result;
  }
  if (tokens.size() > 3) {
    value = MacroValue{};
    value.is_string = true;
    value.string_value = FileTimeToString(std::filesystem::last_write_time(path, error));
    if (error || !SetValue(tokens[3], value, &set_error)) {
      SetError(&result, error ? "filestat could not read modification time" : set_error);
      return result;
    }
  }
  if (tokens.size() > 4) {
    value = MacroValue{};
    value.is_string = true;
    value.string_value = path.root_name().empty() ? path.root_directory().string() : path.root_name().string();
    if (value.string_value.empty()) {
      value.string_value = "?";
    }
    if (!SetValue(tokens[4], value, &set_error)) {
      SetError(&result, set_error);
      return result;
    }
  }

  SetInteger("result", 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteFileStrSeek(const std::vector<std::string>& tokens, bool reverse) {
  MacroStepResult result;
  if (tokens.size() != 3) {
    SetError(&result, reverse ? "filestrseek2 requires a handle and string" :
                                "filestrseek requires a handle and string");
    return result;
  }

  const int handle = EvaluateInteger(tokens[1]);
  auto iter = files_.find(handle);
  if (iter == files_.end() || !iter->second.stream || !iter->second.stream->is_open()) {
    SetError(&result, "file handle is not open");
    return result;
  }

  const std::string needle = EvaluateToken(tokens[2]);
  if (needle.empty()) {
    SetInteger("result", 0);
    return result;
  }

  std::fstream& stream = *iter->second.stream;
  stream.clear();
  const std::streampos original = stream.tellg();
  if (original == std::streampos(-1)) {
    SetInteger("result", 0);
    return result;
  }

  if (reverse) {
    if (original <= std::streampos(0)) {
      SetInteger("result", 0);
      return result;
    }
    stream.seekg(0, std::ios::beg);
    std::string content(static_cast<std::size_t>(original), '\0');
    stream.read(content.data(), static_cast<std::streamsize>(content.size()));
    const std::size_t found = content.rfind(needle);
    if (found == std::string::npos) {
      stream.clear();
      stream.seekg(original);
      stream.seekp(original);
      SetInteger("result", 0);
      return result;
    }
    const std::streampos next = found == 0 ? std::streampos(0) : std::streampos(static_cast<std::streamoff>(found - 1));
    stream.clear();
    stream.seekg(next);
    stream.seekp(next);
    SetInteger("result", 1);
    return result;
  }

  std::string tail((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  const std::size_t found = tail.find(needle);
  if (found == std::string::npos) {
    stream.clear();
    stream.seekg(original);
    stream.seekp(original);
    SetInteger("result", 0);
    return result;
  }

  const std::streampos next = original + static_cast<std::streamoff>(found + needle.size());
  stream.clear();
  stream.seekg(next);
  stream.seekp(next);
  SetInteger("result", 1);
  return result;
}

MacroStepResult MacroRunner::ExecuteFileTruncate(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 3) {
    SetError(&result, "filetruncate requires a filename and size");
    return result;
  }

  const std::filesystem::path path = ResolvePath(EvaluateToken(tokens[1]));
  const int size = EvaluateInteger(tokens[2]);
  if (size < 0) {
    SetInteger("result", -1);
    return result;
  }

  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    std::ofstream create(path, std::ios::binary);
  }
  std::filesystem::resize_file(path, static_cast<std::uintmax_t>(size), error);
  SetInteger("result", error ? -1 : 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteFileWrite(const std::vector<std::string>& tokens, bool line_mode) {
  MacroStepResult result;
  if (tokens.size() < 3) {
    SetError(&result, line_mode ? "filewriteln requires a handle and data" :
                                  "filewrite requires a handle and data");
    return result;
  }

  const int handle = EvaluateInteger(tokens[1]);
  auto iter = files_.find(handle);
  if (iter == files_.end() || !iter->second.stream || !iter->second.stream->is_open()) {
    SetError(&result, "file handle is not open");
    return result;
  }

  std::string data;
  if (tokens.size() == 3 && IsIntegerLiteral(tokens[2])) {
    data.push_back(static_cast<char>(EvaluateInteger(tokens[2]) & 0xff));
  } else {
    data = JoinEvaluatedArguments(tokens, 2, "");
  }
  if (line_mode) {
    data += "\n";
  }

  iter->second.stream->clear();
  iter->second.stream->write(data.data(), static_cast<std::streamsize>(data.size()));
  iter->second.stream->flush();
  const std::streampos next = iter->second.stream->tellp();
  if (next != std::streampos(-1)) {
    iter->second.stream->clear();
    iter->second.stream->seekg(next);
  }
  SetInteger("result", iter->second.stream->good() ? 0 : -1);
  return result;
}

MacroStepResult MacroRunner::ExecuteFileClose(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() < 2) {
    SetError(&result, "fileclose requires a handle");
    return result;
  }
  const int handle = EvaluateInteger(tokens[1]);
  auto iter = files_.find(handle);
  if (iter != files_.end()) {
    ReleaseFileLock(&iter->second);
    files_.erase(iter);
  }
  SetInteger("result", 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteFileLock(const std::vector<std::string>& tokens, bool unlock) {
  MacroStepResult result;
  if ((!unlock && (tokens.size() < 2 || tokens.size() > 3)) || (unlock && tokens.size() != 2)) {
    SetError(&result, unlock ? "fileunlock requires a handle" :
                               "filelock requires a handle and optional timeout");
    return result;
  }

  const int handle = EvaluateInteger(tokens[1]);
  auto iter = files_.find(handle);
  if (iter == files_.end() || !iter->second.stream || !iter->second.stream->is_open()) {
    SetInteger("result", 1);
    return result;
  }

  FileHandle& file = iter->second;
  if (unlock) {
    const bool was_locked = file.locked;
    ReleaseFileLock(&file);
    SetInteger("result", was_locked ? 0 : 1);
    return result;
  }

  if (file.locked) {
    SetInteger("result", 0);
    return result;
  }

#if defined(_WIN32)
  file.locked = true;
  SetInteger("result", 0);
  return result;
#else
  const int fd = open(file.filename.c_str(), O_RDWR);
  if (fd < 0) {
    SetInteger("result", 1);
    return result;
  }

  const bool has_timeout = tokens.size() > 2;
  const int timeout_seconds = has_timeout ? std::max(EvaluateInteger(tokens[2]), 0) : 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
  bool success = false;
  do {
    struct flock lock = {};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    if (fcntl(fd, F_SETLK, &lock) == 0) {
      success = true;
      break;
    }
    if (has_timeout && timeout_seconds == 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } while (!has_timeout || std::chrono::steady_clock::now() < deadline);

  if (!success) {
    close(fd);
    SetInteger("result", 1);
    return result;
  }

  file.lock_fd = fd;
  file.locked = true;
  SetInteger("result", 0);
  return result;
#endif
}

MacroStepResult MacroRunner::ExecuteFolderCreate(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 2) {
    SetError(&result, "foldercreate requires a folder name");
    return result;
  }

  std::error_code error;
  const bool created = std::filesystem::create_directory(ResolvePath(EvaluateToken(tokens[1])), error);
  SetInteger("result", created && !error ? 0 : 2);
  return result;
}

MacroStepResult MacroRunner::ExecuteFolderDelete(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 2) {
    SetError(&result, "folderdelete requires a folder name");
    return result;
  }

  std::error_code error;
  const bool removed = std::filesystem::remove(ResolvePath(EvaluateToken(tokens[1])), error);
  SetInteger("result", removed && !error ? 0 : 2);
  return result;
}

MacroStepResult MacroRunner::ExecuteFindFirst(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  std::string handle_array_name;
  std::string handle_index_expression;
  std::string name_array_name;
  std::string name_index_expression;
  if (tokens.size() != 4 ||
      (!IsIdentifier(tokens[1]) && !ParseArrayReference(tokens[1], &handle_array_name, &handle_index_expression)) ||
      (!IsIdentifier(tokens[3]) && !ParseArrayReference(tokens[3], &name_array_name, &name_index_expression))) {
    SetError(&result, "findfirst requires a directory handle variable, file pattern, and string variable");
    return result;
  }

  const std::string pattern_text = EvaluateToken(tokens[2]).empty() ? "*" : EvaluateToken(tokens[2]);
  const std::filesystem::path pattern_path = ResolvePath(pattern_text);
  const std::filesystem::path directory = pattern_path.parent_path().empty() ?
                                          std::filesystem::current_path() : pattern_path.parent_path();
  const std::string pattern = pattern_path.filename().empty() ? "*" : pattern_path.filename().string();

  DirectoryHandle directory_handle;
  std::error_code error;
  for (std::filesystem::directory_iterator iter(directory, error), end; !error && iter != end; iter.increment(error)) {
    const std::string name = iter->path().filename().string();
    if (WildcardMatchInsensitive(pattern, name)) {
      directory_handle.names.push_back(name);
    }
  }
  std::sort(directory_handle.names.begin(), directory_handle.names.end(), [](const std::string& left,
                                                                             const std::string& right) {
    return Lower(left) < Lower(right);
  });

  MacroValue handle_value;
  MacroValue name_value;
  name_value.is_string = true;
  std::string set_error;
  if (error || directory_handle.names.empty()) {
    handle_value.int_value = -1;
    if (!SetValue(tokens[1], handle_value, &set_error)) {
      SetError(&result, set_error);
      return result;
    }
    name_value.string_value = "";
    if (!SetValue(tokens[3], name_value, &set_error)) {
      SetError(&result, set_error);
      return result;
    }
    SetInteger("result", 0);
    return result;
  }

  name_value.string_value = directory_handle.names.front();
  directory_handle.next_index = 1;
  const int handle = next_directory_handle_++;
  directory_handles_[handle] = std::move(directory_handle);
  handle_value.int_value = handle;
  if (!SetValue(tokens[1], handle_value, &set_error)) {
    directory_handles_.erase(handle);
    SetError(&result, set_error);
    return result;
  }
  if (!SetValue(tokens[3], name_value, &set_error)) {
    directory_handles_.erase(handle);
    SetError(&result, set_error);
    return result;
  }
  SetInteger("result", 1);
  return result;
}

MacroStepResult MacroRunner::ExecuteFindNext(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  std::string name_array_name;
  std::string name_index_expression;
  if (tokens.size() != 3 ||
      (!IsIdentifier(tokens[2]) && !ParseArrayReference(tokens[2], &name_array_name, &name_index_expression))) {
    SetError(&result, "findnext requires a directory handle and string variable");
    return result;
  }

  MacroValue name_value;
  name_value.is_string = true;
  const int handle = EvaluateInteger(tokens[1]);
  auto iter = directory_handles_.find(handle);
  if (iter == directory_handles_.end() || iter->second.next_index >= iter->second.names.size()) {
    name_value.string_value = "";
    std::string error;
    if (!SetValue(tokens[2], name_value, &error)) {
      SetError(&result, error);
      return result;
    }
    SetInteger("result", 0);
    return result;
  }

  name_value.string_value = iter->second.names[iter->second.next_index++];
  std::string error;
  if (!SetValue(tokens[2], name_value, &error)) {
    SetError(&result, error);
    return result;
  }
  SetInteger("result", 1);
  return result;
}

MacroStepResult MacroRunner::ExecuteFindClose(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 2) {
    SetError(&result, "findclose requires a directory handle");
    return result;
  }

  directory_handles_.erase(EvaluateInteger(tokens[1]));
  SetInteger("result", 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteGetDir(const std::vector<std::string>& tokens, bool tera_term_dir) {
  MacroStepResult result;
  if (tokens.size() != 2 || !IsWritableMacroTarget(tokens[1])) {
    SetError(&result, tera_term_dir ? "getttdir requires a string variable" :
                                      "getdir requires a string variable");
    return result;
  }

  MacroValue value;
  value.is_string = true;
  value.string_value = tera_term_dir ? tera_term_directory_ : working_directory_;
  std::string error;
  if (!SetValue(tokens[1], value, &error)) {
    SetError(&result, error);
    return result;
  }
  SetInteger("result", tera_term_dir ? (value.string_value.empty() ? 0 : 1) : 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteSetDir(const std::vector<std::string>& tokens, bool transfer_dir) {
  MacroStepResult result;
  if (tokens.size() != 2) {
    SetError(&result, transfer_dir ? "changedir requires a folder name" :
                                     "setdir requires a folder name");
    return result;
  }

  const std::filesystem::path path = ResolvePath(EvaluateToken(tokens[1]));
  std::error_code error;
  const bool valid = std::filesystem::is_directory(path, error) && !error;
  if (valid) {
    if (transfer_dir) {
      transfer_directory_ = path.lexically_normal().string();
    } else {
      working_directory_ = path.lexically_normal().string();
    }
  }
  SetInteger("result", valid ? 0 : 1);
  return result;
}

MacroStepResult MacroRunner::ExecuteGetEnv(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 3 || !IsWritableMacroTarget(tokens[2])) {
    SetError(&result, "getenv requires an environment name and string variable");
    return result;
  }

  const std::string name = EvaluateToken(tokens[1]);
  const char* env_value = std::getenv(name.c_str());
  MacroValue value;
  value.is_string = true;
  value.string_value = env_value == nullptr ? "" : env_value;
  std::string error;
  if (!SetValue(tokens[2], value, &error)) {
    SetError(&result, error);
  }
  return result;
}

MacroStepResult MacroRunner::ExecuteExpandEnv(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if ((tokens.size() != 2 && tokens.size() != 3) || !IsWritableMacroTarget(tokens[1])) {
    SetError(&result, "expandenv requires a string variable and optional source string");
    return result;
  }

  std::string source;
  if (tokens.size() == 3) {
    source = EvaluateToken(tokens[2]);
  } else {
    MacroValue current_value;
    if (ResolveArrayValue(tokens[1], &current_value)) {
      source = current_value.is_string ? current_value.string_value : std::to_string(current_value.int_value);
    } else {
      const auto iter = variables_.find(NormalizeName(tokens[1]));
      source = iter == variables_.end() ? "" :
               (iter->second.is_string ? iter->second.string_value : std::to_string(iter->second.int_value));
    }
  }

  MacroValue value;
  value.is_string = true;
  value.string_value = ExpandPercentEnvironmentVariables(source);
  std::string error;
  if (!SetValue(tokens[1], value, &error)) {
    SetError(&result, error);
    return result;
  }
  SetInteger("result", 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteGetDateTime(const std::vector<std::string>& tokens, bool date_mode) {
  MacroStepResult result;
  if (tokens.size() < 2 || tokens.size() > 4 || !IsWritableMacroTarget(tokens[1])) {
    SetError(&result, date_mode ? "getdate requires a string variable and optional format/timezone" :
                                  "gettime requires a string variable and optional format/timezone");
    return result;
  }

  const std::string format = tokens.size() > 2 ? EvaluateToken(tokens[2]) :
                             (date_mode ? "%Y-%m-%d" : "%H:%M:%S");
  const std::string timezone = tokens.size() > 3 ? EvaluateToken(tokens[3]) : "";
  const std::string* timezone_arg = tokens.size() > 3 ? &timezone : nullptr;
  std::string formatted;
  int result_code = 0;
  if (!FormatLocalDateTime(format, timezone_arg, &formatted, &result_code)) {
    SetInteger("result", result_code);
    return result;
  }

  MacroValue value;
  value.is_string = true;
  value.string_value = formatted;
  std::string error;
  if (!SetValue(tokens[1], value, &error)) {
    SetError(&result, error);
    return result;
  }
  SetInteger("result", 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteUptime(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 2 || !IsWritableMacroTarget(tokens[1])) {
    SetError(&result, "uptime requires an integer variable");
    return result;
  }

  const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
  MacroValue value;
  value.int_value = static_cast<int>(milliseconds % INT_MAX);
  std::string error;
  if (!SetValue(tokens[1], value, &error)) {
    SetError(&result, error);
    return result;
  }
  SetInteger("result", 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteLogOpen(TerminalSession* session, const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() < 4 || tokens.size() > 9) {
    SetError(&result, "logopen requires filename, binary flag, append flag, and optional flags");
    return result;
  }

  FlushLogOutput(session);
  if (log_.stream && log_.stream->is_open()) {
    SetInteger("result", 1);
    return result;
  }

  const std::filesystem::path path = ResolveTransferPath(EvaluateToken(tokens[1]));
  const bool append = EvaluateInteger(tokens[3]) != 0;
  auto stream = std::make_unique<std::ofstream>(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
  if (!stream->is_open()) {
    SetInteger("result", 1);
    return result;
  }

  log_ = LogState{};
  log_.stream = std::move(stream);
  log_.filename = path.string();
  log_.binary = EvaluateInteger(tokens[2]) != 0;
  log_.append = append;
  log_.plain_text = tokens.size() > 4 && EvaluateInteger(tokens[4]) != 0;
  log_.timestamp = tokens.size() > 5 && EvaluateInteger(tokens[5]) != 0;
  log_.hide_dialog = tokens.size() > 6 && EvaluateInteger(tokens[6]) != 0;
  log_.include_screen_buffer = tokens.size() > 7 && EvaluateInteger(tokens[7]) != 0;
  log_.timestamp_type = tokens.size() > 8 ? EvaluateInteger(tokens[8]) : 0;
  log_.auto_close = log_auto_close_mode_;
  log_.history_offset = session == nullptr ? 0 : session->OutputHistory().size();

  if (!log_.binary && log_.timestamp && log_.stream) {
    const std::string timestamp = CurrentLogTimestamp(log_.timestamp_type == 1);
    log_.stream->write(timestamp.data(), static_cast<std::streamsize>(timestamp.size()));
  }
  if (log_.include_screen_buffer && session != nullptr && log_.stream) {
    const std::string screen_buffer = PrepareLogData(session->OutputHistory());
    log_.stream->write(screen_buffer.data(), static_cast<std::streamsize>(screen_buffer.size()));
    log_.history_offset = session->OutputHistory().size();
  }
  if (log_.stream) {
    log_.stream->flush();
  }
  RotateLogIfNeeded();

  SetInteger("result", log_.stream && log_.stream->good() ? 0 : 1);
  return result;
}

MacroStepResult MacroRunner::ExecuteLogClose(TerminalSession* session, const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 1) {
    SetError(&result, "logclose does not accept arguments");
    return result;
  }

  FlushLogOutput(session);
  if (log_.stream) {
    log_.stream->close();
  }
  log_ = LogState{};
  SetInteger("result", 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteLogWrite(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() < 2) {
    SetError(&result, "logwrite requires text");
    return result;
  }
  if (!log_.stream || !log_.stream->is_open()) {
    SetInteger("result", 1);
    return result;
  }

  const std::string text = PrepareLogData(JoinEvaluatedArguments(tokens, 1, ""));
  log_.stream->write(text.data(), static_cast<std::streamsize>(text.size()));
  log_.stream->flush();
  RotateLogIfNeeded();
  SetInteger("result", log_.stream && log_.stream->good() ? 0 : 1);
  return result;
}

MacroStepResult MacroRunner::ExecuteLogControl(TerminalSession* session,
                                               const std::vector<std::string>& tokens,
                                               bool pause) {
  MacroStepResult result;
  if (tokens.size() != 1) {
    SetError(&result, pause ? "logpause does not accept arguments" :
                              "logstart does not accept arguments");
    return result;
  }

  FlushLogOutput(session);
  if (log_.stream && log_.stream->is_open()) {
    log_.paused = pause;
  }
  SetInteger("result", 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteLogInfo(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 2 || !IsWritableMacroTarget(tokens[1])) {
    SetError(&result, "loginfo requires a string variable");
    return result;
  }

  MacroValue filename;
  filename.is_string = true;
  filename.string_value = (log_.stream && log_.stream->is_open()) ? log_.filename : "";
  std::string error;
  if (!SetValue(tokens[1], filename, &error)) {
    SetError(&result, error);
    return result;
  }

  if (!log_.stream || !log_.stream->is_open()) {
    SetInteger("result", -1);
    return result;
  }

  int flags = 0;
  if (log_.binary) {
    flags |= 1;
  }
  if (log_.append) {
    flags |= 2;
  }
  if (log_.plain_text) {
    flags |= 4;
  }
  if (log_.timestamp) {
    flags |= 8;
  }
  if (log_.hide_dialog) {
    flags |= 16;
  }
  SetInteger("result", flags);
  return result;
}

MacroStepResult MacroRunner::ExecuteLogRotate(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() < 2 || tokens.size() > 3) {
    SetError(&result, "logrotate requires 'size', 'rotate', or 'halt'");
    return result;
  }
  if (!log_.stream || !log_.stream->is_open()) {
    SetInteger("result", 1);
    return result;
  }

  const std::string mode = Lower(EvaluateToken(tokens[1]));
  if (mode == "halt") {
    if (tokens.size() != 2) {
      SetError(&result, "logrotate halt does not accept a value");
      return result;
    }
    log_.rotate_enabled = false;
    SetInteger("result", 0);
    return result;
  }

  if (tokens.size() != 3) {
    SetError(&result, "logrotate size/rotate requires a value");
    return result;
  }

  if (mode == "size") {
    std::uintmax_t rotate_size = 0;
    if (!ParseLogRotateSize(EvaluateToken(tokens[2]), &rotate_size)) {
      SetInteger("result", 1);
      return result;
    }
    log_.rotate_size = rotate_size;
    log_.rotate_count = log_.rotate_count <= 0 ? 1 : log_.rotate_count;
    log_.rotate_enabled = true;
    RotateLogIfNeeded();
    SetInteger("result", log_.stream && log_.stream->good() ? 0 : 1);
    return result;
  }

  if (mode == "rotate") {
    const int count = EvaluateInteger(tokens[2]);
    if (count <= 0) {
      SetInteger("result", 1);
      return result;
    }
    log_.rotate_count = count;
    log_.rotate_enabled = log_.rotate_size > 0;
    RotateLogIfNeeded();
    SetInteger("result", log_.stream && log_.stream->good() ? 0 : 1);
    return result;
  }

  SetError(&result, "unknown logrotate mode: " + tokens[1]);
  return result;
}

MacroStepResult MacroRunner::ExecuteLogAutoCloseMode(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() != 2) {
    SetError(&result, "logautoclosemode requires a flag");
    return result;
  }

  log_auto_close_mode_ = EvaluateInteger(tokens[1]) != 0;
  if (log_.stream && log_.stream->is_open()) {
    log_.auto_close = log_auto_close_mode_;
  }
  SetInteger("result", 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteFileNameBox(const std::vector<std::string>& tokens, bool directory_mode) {
  MacroStepResult result;
  const std::string command = directory_mode ? "dirnamebox" : "filenamebox";
  const std::size_t minimum = 2;
  const std::size_t maximum = directory_mode ? 3 : 4;
  if (tokens.size() < minimum || tokens.size() > maximum) {
    SetError(&result, command + " requires a title and optional mode/initial directory");
    return result;
  }

  const std::string title = EvaluateToken(tokens[1]);
  const bool save_dialog = !directory_mode && tokens.size() > 2 && EvaluateInteger(tokens[2]) != 0;
  const std::size_t initial_dir_index = directory_mode ? 2 : 3;
  const std::string initial_dir = tokens.size() > initial_dir_index ? EvaluateToken(tokens[initial_dir_index]) : "";

  wait_ = WaitState{};
  wait_.kind = WaitKind::Dialog;
  wait_.dialog_type = command;
  wait_.dialog_title = title;
  wait_.dialog_text = initial_dir;
  wait_.dialog_default_text = initial_dir;
  wait_.dialog_save = save_dialog;
  SetString("inputstr", "");
  result = CheckWait(nullptr);
  return result;
}

MacroStepResult MacroRunner::ExecuteListBox(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  if (tokens.size() < 4 || !IsIdentifier(tokens[3])) {
    SetError(&result, "listbox requires a message, title, and string array");
    return result;
  }

  const auto array_iter = string_arrays_.find(NormalizeName(tokens[3]));
  if (array_iter == string_arrays_.end()) {
    SetError(&result, "listbox string array is not defined: " + tokens[3]);
    return result;
  }

  int selected = 0;
  for (std::size_t index = 4; index < tokens.size(); ++index) {
    const std::string token = EvaluateToken(tokens[index]);
    char* end = nullptr;
    const long parsed = std::strtol(token.c_str(), &end, 10);
    if (end != nullptr && *end == '\0') {
      selected = static_cast<int>(parsed);
      break;
    }
  }
  if (selected < 0 || selected >= static_cast<int>(array_iter->second.size())) {
    selected = array_iter->second.empty() ? -1 : 0;
  }

  wait_ = WaitState{};
  wait_.kind = WaitKind::Dialog;
  wait_.dialog_type = "listbox";
  wait_.dialog_title = EvaluateToken(tokens[2]);
  wait_.dialog_text = EvaluateToken(tokens[1]);
  wait_.dialog_items = array_iter->second;
  wait_.dialog_selected_index = selected;
  wait_.dialog_default_text = selected >= 0 ? std::to_string(selected) : "";
  result = CheckWait(nullptr);
  return result;
}

MacroStepResult MacroRunner::ExecuteStatusBox(const std::vector<std::string>& tokens, bool close) {
  MacroStepResult result;
  if (close) {
    if (tokens.size() != 1) {
      SetError(&result, "closesbox does not accept arguments");
      return result;
    }
    pending_status_box_action_ = "close";
    pending_status_box_title_.clear();
    pending_status_box_text_.clear();
    SetInteger("result", 0);
    return result;
  }

  if (tokens.size() < 3 || tokens.size() > 4) {
    SetError(&result, "statusbox requires message, title, and optional special flag");
    return result;
  }

  std::string message = EvaluateToken(tokens[1]);
  if (tokens.size() > 3 && EvaluateInteger(tokens[3]) != 0) {
    message = DecodeSpecialDialogText(message);
  }
  pending_status_box_action_ = "show";
  pending_status_box_title_ = EvaluateToken(tokens[2]);
  pending_status_box_text_ = message;
  SetInteger("result", 0);
  return result;
}

MacroStepResult MacroRunner::ExecuteDialog(const std::vector<std::string>& tokens) {
  MacroStepResult result;
  const std::string command = Lower(tokens[0]);
  const bool input_dialog = command == "inputbox";
  const bool password_dialog = command == "passwordbox";
  const std::size_t minimum = input_dialog ? 3 : 3;
  const std::size_t maximum = input_dialog ? 5 : 4;
  if (tokens.size() < minimum || tokens.size() > maximum) {
    SetError(&result, command + " requires message and title");
    return result;
  }

  std::string message = EvaluateToken(tokens[1]);
  const std::string title = EvaluateToken(tokens[2]);
  const std::string default_text = input_dialog && tokens.size() >= 4 ? EvaluateToken(tokens[3]) : "";
  const std::size_t special_index = input_dialog ? 4 : 3;
  if (tokens.size() > special_index && EvaluateInteger(tokens[special_index]) != 0) {
    message = DecodeSpecialDialogText(message);
  }

  wait_ = WaitState{};
  wait_.kind = WaitKind::Dialog;
  wait_.dialog_type = command;
  wait_.dialog_title = title;
  wait_.dialog_text = message;
  wait_.dialog_default_text = default_text;
  wait_.dialog_value = default_text;
  wait_.dialog_password = password_dialog;
  result = CheckWait(nullptr);
  return result;
}

bool MacroRunner::SkipToConditionalBranch(std::string* error) {
  int depth = 0;
  for (std::size_t index = next_line_; index < lines_.size(); ++index) {
    const std::string line = Trim(StripComment(lines_[index]));
    if (line.empty() || line.front() == ':') {
      continue;
    }

    const std::vector<std::string> tokens = Tokenize(line);
    if (tokens.empty()) {
      continue;
    }
    const std::string command = Lower(tokens[0]);
    if (command == "if" && IsBlockIfTokens(tokens)) {
      ++depth;
      continue;
    }
    if (command == "endif") {
      if (depth == 0) {
        next_line_ = index + 1;
        return true;
      }
      --depth;
      continue;
    }
    if (depth != 0) {
      continue;
    }
    if (command == "elseif") {
      const std::size_t then_index = ThenTokenIndex(tokens);
      const std::size_t expression_end = then_index == std::string::npos ? tokens.size() : then_index;
      if (expression_end <= 1) {
        if (error != nullptr) {
          *error = "elseif requires an expression";
        }
        return false;
      }
      if (EvaluateCondition(JoinTokens(tokens, 1, expression_end))) {
        next_line_ = index + 1;
        return true;
      }
      continue;
    }
    if (command == "else") {
      next_line_ = index + 1;
      return true;
    }
  }

  if (error != nullptr) {
    *error = "if block is missing endif";
  }
  return false;
}

bool MacroRunner::SkipToEndIf(std::string* error) {
  int depth = 0;
  for (std::size_t index = next_line_; index < lines_.size(); ++index) {
    const std::string line = Trim(StripComment(lines_[index]));
    if (line.empty() || line.front() == ':') {
      continue;
    }

    if (IsBlockIfLine(line)) {
      ++depth;
      continue;
    }

    if (FirstCommand(line) == "endif") {
      if (depth == 0) {
        next_line_ = index + 1;
        return true;
      }
      --depth;
    }
  }

  if (error != nullptr) {
    *error = "if block is missing endif";
  }
  return false;
}

bool MacroRunner::SkipToEndWhile(std::string* error) {
  int depth = 0;
  for (std::size_t index = next_line_; index < lines_.size(); ++index) {
    const std::string line = Trim(StripComment(lines_[index]));
    if (line.empty() || line.front() == ':') {
      continue;
    }

    const std::string command = FirstCommand(line);
    if (command == "while") {
      ++depth;
      continue;
    }
    if (command == "endwhile") {
      if (depth == 0) {
        next_line_ = index + 1;
        return true;
      }
      --depth;
    }
  }

  if (error != nullptr) {
    *error = "while block is missing endwhile";
  }
  return false;
}

bool MacroRunner::JumpBackToWhile(std::string* error) {
  if (next_line_ < 2) {
    if (error != nullptr) {
      *error = "endwhile is missing while";
    }
    return false;
  }

  int depth = 0;
  for (std::size_t index = next_line_ - 1; index > 0; --index) {
    const std::size_t line_index = index - 1;
    const std::string line = Trim(StripComment(lines_[line_index]));
    if (line.empty() || line.front() == ':') {
      continue;
    }

    const std::string command = FirstCommand(line);
    if (command == "endwhile") {
      ++depth;
      continue;
    }
    if (command == "while") {
      if (depth == 0) {
        next_line_ = line_index;
        return true;
      }
      --depth;
    }
  }

  if (error != nullptr) {
    *error = "endwhile is missing while";
  }
  return false;
}

bool MacroRunner::SkipToEndUntil(std::string* error) {
  int depth = 0;
  for (std::size_t index = next_line_; index < lines_.size(); ++index) {
    const std::string line = Trim(StripComment(lines_[index]));
    if (line.empty() || line.front() == ':') {
      continue;
    }

    const std::string command = FirstCommand(line);
    if (command == "until") {
      ++depth;
      continue;
    }
    if (command == "enduntil") {
      if (depth == 0) {
        next_line_ = index + 1;
        return true;
      }
      --depth;
    }
  }

  if (error != nullptr) {
    *error = "until block is missing enduntil";
  }
  return false;
}

bool MacroRunner::JumpBackToUntil(std::string* error) {
  if (next_line_ < 2) {
    if (error != nullptr) {
      *error = "enduntil is missing until";
    }
    return false;
  }

  int depth = 0;
  for (std::size_t index = next_line_ - 1; index > 0; --index) {
    const std::size_t line_index = index - 1;
    const std::string line = Trim(StripComment(lines_[line_index]));
    if (line.empty() || line.front() == ':') {
      continue;
    }

    const std::string command = FirstCommand(line);
    if (command == "enduntil") {
      ++depth;
      continue;
    }
    if (command == "until") {
      if (depth == 0) {
        next_line_ = line_index;
        return true;
      }
      --depth;
    }
  }

  if (error != nullptr) {
    *error = "enduntil is missing until";
  }
  return false;
}

bool MacroRunner::SkipToLoop(std::string* error) {
  int depth = 0;
  for (std::size_t index = next_line_; index < lines_.size(); ++index) {
    const std::string line = Trim(StripComment(lines_[index]));
    if (line.empty() || line.front() == ':') {
      continue;
    }

    const std::string command = FirstCommand(line);
    if (command == "do") {
      ++depth;
      continue;
    }
    if (command == "loop") {
      if (depth == 0) {
        next_line_ = index + 1;
        return true;
      }
      --depth;
    }
  }

  if (error != nullptr) {
    *error = "do block is missing loop";
  }
  return false;
}

bool MacroRunner::JumpBackToDo(std::string* error) {
  if (next_line_ < 2) {
    if (error != nullptr) {
      *error = "loop is missing do";
    }
    return false;
  }

  int depth = 0;
  for (std::size_t index = next_line_ - 1; index > 0; --index) {
    const std::size_t line_index = index - 1;
    const std::string line = Trim(StripComment(lines_[line_index]));
    if (line.empty() || line.front() == ':') {
      continue;
    }

    const std::string command = FirstCommand(line);
    if (command == "loop") {
      ++depth;
      continue;
    }
    if (command == "do") {
      if (depth == 0) {
        next_line_ = line_index;
        return true;
      }
      --depth;
    }
  }

  if (error != nullptr) {
    *error = "loop is missing do";
  }
  return false;
}

bool MacroRunner::MoveToNearestLoopBoundary(bool after_boundary,
                                            std::string* boundary_command,
                                            std::string* error) {
  int depth = 0;
  for (std::size_t index = next_line_; index < lines_.size(); ++index) {
    const std::string line = Trim(StripComment(lines_[index]));
    if (line.empty() || line.front() == ':') {
      continue;
    }

    const std::string command = FirstCommand(line);
    if (command == "while" || command == "until" || command == "do" || command == "for") {
      ++depth;
      continue;
    }
    if (command != "endwhile" && command != "enduntil" && command != "loop" && command != "next") {
      continue;
    }

    if (depth == 0) {
      if (boundary_command != nullptr) {
        *boundary_command = command;
      }
      next_line_ = after_boundary ? index + 1 : index;
      return true;
    }
    --depth;
  }

  if (error != nullptr) {
    *error = "loop control command is outside a loop";
  }
  return false;
}

bool MacroRunner::JumpToLabel(const std::string& label, std::string* error) {
  const auto iter = labels_.find(NormalizeName(label));
  if (iter == labels_.end()) {
    if (error != nullptr) {
      *error = "label is not defined: " + label;
    }
    return false;
  }
  next_line_ = iter->second;
  return true;
}

bool MacroRunner::AssignLine(const std::string& line, std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  const std::size_t equals = FindAssignmentEquals(line);
  if (equals == std::string::npos) {
    return false;
  }

  const std::string name = Trim(line.substr(0, equals));
  std::string array_name;
  std::string index_expression;
  if (!IsIdentifier(name) && !ParseArrayReference(name, &array_name, &index_expression)) {
    if (error != nullptr) {
      *error = "assignment requires a variable name";
    }
    return false;
  }

  const MacroValue value = EvaluateValue(line.substr(equals + 1));
  if (!SetValue(name, value, error)) {
    return false;
  }
  return true;
}

bool MacroRunner::EvaluateCondition(const std::string& expression) {
  std::string value = Trim(expression);
  if (!value.empty() && value.front() == '!') {
    return !EvaluateCondition(value.substr(1));
  }

  struct Operator {
    const char* text;
  };
  static constexpr Operator kOperators[] = {{"=="}, {"!="}, {"<>"}, {">="}, {"<="}, {"="}, {">"}, {"<"}};
  for (const Operator& op : kOperators) {
    const std::size_t index = FindOutsideQuotes(value, op.text);
    if (index == std::string::npos) {
      continue;
    }
    const MacroValue lhs = EvaluateValue(value.substr(0, index));
    const MacroValue rhs = EvaluateValue(value.substr(index + std::string(op.text).size()));
    const std::string op_text = op.text;
    if (lhs.is_string || rhs.is_string) {
      const std::string left = lhs.is_string ? lhs.string_value : std::to_string(lhs.int_value);
      const std::string right = rhs.is_string ? rhs.string_value : std::to_string(rhs.int_value);
      if (op_text == "=" || op_text == "==") {
        return left == right;
      }
      if (op_text == "!=" || op_text == "<>") {
        return left != right;
      }
      if (op_text == ">") {
        return left > right;
      }
      if (op_text == "<") {
        return left < right;
      }
      if (op_text == ">=") {
        return left >= right;
      }
      if (op_text == "<=") {
        return left <= right;
      }
    }

    const int left = lhs.is_string ? std::atoi(lhs.string_value.c_str()) : lhs.int_value;
    const int right = rhs.is_string ? std::atoi(rhs.string_value.c_str()) : rhs.int_value;
    if (op_text == "=" || op_text == "==") {
      return left == right;
    }
    if (op_text == "!=" || op_text == "<>") {
      return left != right;
    }
    if (op_text == ">") {
      return left > right;
    }
    if (op_text == "<") {
      return left < right;
    }
    if (op_text == ">=") {
      return left >= right;
    }
    if (op_text == "<=") {
      return left <= right;
    }
  }

  const MacroValue plain = EvaluateValue(value);
  return plain.is_string ? !plain.string_value.empty() : plain.int_value != 0;
}

MacroRunner::MacroValue MacroRunner::EvaluateValue(const std::string& expression) {
  const std::string trimmed = Trim(expression);
  const std::vector<std::string> tokens = Tokenize(trimmed);
  if (tokens.size() > 1 && std::find(tokens.begin(), tokens.end(), "+") == tokens.end() &&
      std::find(tokens.begin(), tokens.end(), "-") == tokens.end() &&
      std::find(tokens.begin(), tokens.end(), "*") == tokens.end() &&
      std::find(tokens.begin(), tokens.end(), "/") == tokens.end() &&
      std::find(tokens.begin(), tokens.end(), "%") == tokens.end() &&
      std::find(tokens.begin(), tokens.end(), "&") == tokens.end()) {
    MacroValue value;
    value.is_string = true;
    value.string_value = JoinEvaluatedArguments(tokens, 0, "");
    return value;
  }

  if (IsQuoted(trimmed)) {
    MacroValue value;
    value.is_string = true;
    value.string_value = tokens.empty() ? UnquoteAndDecode(trimmed) : tokens[0];
    return value;
  }

  MacroValue array_value;
  if (ResolveArrayValue(trimmed, &array_value)) {
    return array_value;
  }

  const auto iter = variables_.find(NormalizeName(trimmed));
  if (iter != variables_.end()) {
    return iter->second;
  }

  if (IsIntegerLiteral(trimmed) ||
      FindTopLevelOperatorRight(trimmed, "&+-*/%") != std::string::npos ||
      HasWrappingParentheses(trimmed)) {
    MacroValue value;
    value.int_value = EvaluateInteger(trimmed);
    return value;
  }

  MacroValue value;
  value.is_string = true;
  value.string_value = tokens.empty() ? UnquoteAndDecode(trimmed) : tokens[0];
  return value;
}

int MacroRunner::EvaluateInteger(const std::string& expression) const {
  const std::string trimmed = Trim(expression);
  if (trimmed.empty()) {
    return 0;
  }

  if (HasWrappingParentheses(trimmed)) {
    return EvaluateInteger(trimmed.substr(1, trimmed.size() - 2));
  }

  std::size_t op = FindTopLevelOperatorRight(trimmed, "&");
  if (op != std::string::npos) {
    return EvaluateInteger(trimmed.substr(0, op)) & EvaluateInteger(trimmed.substr(op + 1));
  }
  op = FindTopLevelOperatorRight(trimmed, "+-");
  if (op != std::string::npos) {
    return trimmed[op] == '+' ? EvaluateInteger(trimmed.substr(0, op)) + EvaluateInteger(trimmed.substr(op + 1)) :
                                EvaluateInteger(trimmed.substr(0, op)) - EvaluateInteger(trimmed.substr(op + 1));
  }
  op = FindTopLevelOperatorRight(trimmed, "*/%");
  if (op != std::string::npos) {
    const int right = EvaluateInteger(trimmed.substr(op + 1));
    if (trimmed[op] == '*') {
      return EvaluateInteger(trimmed.substr(0, op)) * right;
    }
    if (trimmed[op] == '%') {
      return right == 0 ? 0 : EvaluateInteger(trimmed.substr(0, op)) % right;
    }
    return right == 0 ? 0 : EvaluateInteger(trimmed.substr(0, op)) / right;
  }

  MacroValue array_value;
  if (ResolveArrayValue(trimmed, &array_value)) {
    return array_value.is_string ? std::atoi(array_value.string_value.c_str()) : array_value.int_value;
  }

  const auto iter = variables_.find(NormalizeName(trimmed));
  if (iter != variables_.end()) {
    return iter->second.is_string ? std::atoi(iter->second.string_value.c_str()) : iter->second.int_value;
  }
  if (IsQuoted(trimmed)) {
    return std::atoi(UnquoteAndDecode(trimmed).c_str());
  }
  if (!trimmed.empty() && trimmed.front() == '$') {
    return static_cast<int>(std::strtol(trimmed.c_str() + 1, nullptr, 16));
  }
  return static_cast<int>(std::strtol(trimmed.c_str(), nullptr, 0));
}

std::string MacroRunner::EvaluateToken(const std::string& token) {
  MacroValue array_value;
  if (ResolveArrayValue(token, &array_value)) {
    return array_value.is_string ? array_value.string_value : std::to_string(array_value.int_value);
  }
  const auto iter = variables_.find(NormalizeName(token));
  if (iter == variables_.end()) {
    return token;
  }
  return iter->second.is_string ? iter->second.string_value : std::to_string(iter->second.int_value);
}

std::string MacroRunner::JoinEvaluatedArguments(const std::vector<std::string>& tokens,
                                                std::size_t begin,
                                                const std::string& delimiter) {
  std::string result;
  for (std::size_t i = begin; i < tokens.size(); ++i) {
    if (!result.empty()) {
      result += delimiter;
    }
    result += EvaluateToken(tokens[i]);
  }
  return result;
}

std::filesystem::path MacroRunner::ResolvePath(const std::string& value) const {
  const std::filesystem::path path(value);
  if (path.is_absolute()) {
    return path.lexically_normal();
  }
  return std::filesystem::absolute(std::filesystem::path(working_directory_) / path).lexically_normal();
}

std::filesystem::path MacroRunner::ResolveTransferPath(const std::string& value) const {
  const std::filesystem::path path(value);
  if (path.is_absolute()) {
    return path.lexically_normal();
  }
  return std::filesystem::absolute(std::filesystem::path(transfer_directory_) / path).lexically_normal();
}

std::string MacroRunner::PrepareLogData(const std::string& bytes) const {
  if (log_.binary) {
    return bytes;
  }

  std::string text = StripAnsiSequences(bytes);
  if (log_.plain_text) {
    text = FilterPlainLogText(text);
  }
  return text;
}

void MacroRunner::FlushLogOutput(TerminalSession* session) {
  if (session == nullptr || !log_.stream || !log_.stream->is_open()) {
    return;
  }

  const std::string& history = session->OutputHistory();
  if (log_.history_offset > history.size()) {
    log_.history_offset = history.size();
  }
  if (log_.paused || log_.history_offset == history.size()) {
    log_.history_offset = history.size();
    return;
  }

  const std::string chunk = PrepareLogData(history.substr(log_.history_offset));
  log_.history_offset = history.size();
  log_.stream->write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
  log_.stream->flush();
  RotateLogIfNeeded();
}

void MacroRunner::RotateLogIfNeeded() {
  if (!log_.rotate_enabled || log_.rotate_size == 0 || log_.rotate_count <= 0 ||
      !log_.stream || !log_.stream->is_open() || log_.filename.empty()) {
    return;
  }

  std::error_code error;
  const std::filesystem::path current_path(log_.filename);
  const std::uintmax_t current_size = std::filesystem::exists(current_path, error) && !error ?
                                      std::filesystem::file_size(current_path, error) : 0;
  if (error || current_size <= log_.rotate_size) {
    return;
  }

  log_.stream->close();
  for (int index = log_.rotate_count; index >= 1; --index) {
    const std::filesystem::path target = log_.filename + "." + std::to_string(index);
    if (index == log_.rotate_count) {
      std::filesystem::remove(target, error);
    }
    const std::filesystem::path source = index == 1 ? current_path :
                                         std::filesystem::path(log_.filename + "." + std::to_string(index - 1));
    if (std::filesystem::exists(source, error) && !error) {
      std::filesystem::rename(source, target, error);
    }
  }

  log_.stream = std::make_unique<std::ofstream>(current_path, std::ios::binary | std::ios::trunc);
}

void MacroRunner::CloseAutoLogIfDone(TerminalSession* session) {
  if (!done_ || !log_.auto_close || !log_.stream || !log_.stream->is_open()) {
    return;
  }

  FlushLogOutput(session);
  log_.stream->close();
  log_ = LogState{};
}

void MacroRunner::ReleaseFileLock(FileHandle* handle) {
  if (handle == nullptr || !handle->locked) {
    return;
  }

#if !defined(_WIN32)
  if (handle->lock_fd >= 0) {
    struct flock lock = {};
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    fcntl(handle->lock_fd, F_SETLK, &lock);
    close(handle->lock_fd);
  }
#endif
  handle->lock_fd = -1;
  handle->locked = false;
}

bool MacroRunner::FormatString(const std::string& format,
                               const std::vector<std::string>& tokens,
                               std::size_t argument_index,
                               std::string* output,
                               int* result_code) {
  std::string result;
  for (std::size_t index = 0; index < format.size(); ++index) {
    if (format[index] != '%') {
      result.push_back(format[index]);
      continue;
    }
    if (index + 1 < format.size() && format[index + 1] == '%') {
      result.push_back('%');
      ++index;
      continue;
    }

    PrintfSpec spec;
    ++index;
    bool reading_flags = true;
    while (reading_flags && index < format.size()) {
      switch (format[index]) {
        case '-':
          spec.left = true;
          ++index;
          break;
        case '+':
          spec.plus = true;
          ++index;
          break;
        case ' ':
          spec.space = true;
          ++index;
          break;
        case '#':
          spec.alternate = true;
          ++index;
          break;
        case '0':
          spec.zero = true;
          ++index;
          break;
        default:
          reading_flags = false;
          break;
      }
    }

    if (index < format.size() && format[index] == '*') {
      if (argument_index >= tokens.size()) {
        if (result_code != nullptr) {
          *result_code = 3;
        }
        return false;
      }
      spec.width = EvaluateInteger(tokens[argument_index++]);
      if (spec.width < 0) {
        spec.left = true;
        spec.width = -spec.width;
      }
      ++index;
    } else {
      while (index < format.size() && std::isdigit(static_cast<unsigned char>(format[index])) != 0) {
        spec.width = spec.width * 10 + (format[index] - '0');
        ++index;
      }
    }

    if (index < format.size() && format[index] == '.') {
      ++index;
      spec.precision = 0;
      if (index < format.size() && format[index] == '*') {
        if (argument_index >= tokens.size()) {
          if (result_code != nullptr) {
            *result_code = 3;
          }
          return false;
        }
        spec.precision = EvaluateInteger(tokens[argument_index++]);
        if (spec.precision < 0) {
          spec.precision = -1;
        }
        ++index;
      } else {
        while (index < format.size() && std::isdigit(static_cast<unsigned char>(format[index])) != 0) {
          spec.precision = spec.precision * 10 + (format[index] - '0');
          ++index;
        }
      }
    }

    if (index >= format.size()) {
      if (result_code != nullptr) {
        *result_code = 2;
      }
      return false;
    }

    spec.type = format[index];
    if (spec.type == 'l' || spec.type == 'h') {
      ++index;
      if (index >= format.size()) {
        if (result_code != nullptr) {
          *result_code = 2;
        }
        return false;
      }
      spec.type = format[index];
    }

    if (argument_index >= tokens.size()) {
      if (result_code != nullptr) {
        *result_code = 3;
      }
      return false;
    }

    switch (spec.type) {
      case 's':
        result += FormatStringValue(EvaluateToken(tokens[argument_index++]), spec);
        break;
      case 'c': {
        const std::string text = EvaluateToken(tokens[argument_index]);
        int value = text.size() == 1 ? static_cast<unsigned char>(text[0]) : EvaluateInteger(tokens[argument_index]);
        ++argument_index;
        result += FormatStringValue(std::string(1, static_cast<char>(value & 0xff)), spec);
        break;
      }
      case 'd':
      case 'i':
        result += FormatSigned(EvaluateInteger(tokens[argument_index++]), spec);
        break;
      case 'u':
        result += FormatUnsigned(static_cast<unsigned int>(EvaluateInteger(tokens[argument_index++])), 10, false, spec);
        break;
      case 'o':
        result += FormatUnsigned(static_cast<unsigned int>(EvaluateInteger(tokens[argument_index++])), 8, false, spec);
        break;
      case 'x':
      case 'X':
        result += FormatUnsigned(static_cast<unsigned int>(EvaluateInteger(tokens[argument_index++])),
                                 16, spec.type == 'X', spec);
        break;
      case 'f':
      case 'e':
      case 'E':
      case 'g':
      case 'G': {
        const std::string text = EvaluateToken(tokens[argument_index++]);
        result += FormatDouble(std::strtod(text.c_str(), nullptr), spec);
        break;
      }
      default:
        if (result_code != nullptr) {
          *result_code = 2;
        }
        return false;
    }
  }

  if (output != nullptr) {
    *output = result;
  }
  if (result_code != nullptr) {
    *result_code = 0;
  }
  return true;
}

void MacroRunner::SetInteger(const std::string& name, int value) {
  variables_[NormalizeName(name)] = MacroValue{false, value, ""};
}

void MacroRunner::SetString(const std::string& name, const std::string& value) {
  variables_[NormalizeName(name)] = MacroValue{true, 0, value};
}

void MacroRunner::ClearGroupMatchStrings() {
  for (int index = 1; index <= 9; ++index) {
    SetString("groupmatchstr" + std::to_string(index), "");
  }
}

void MacroRunner::SetRegexMatchVariables(const std::smatch& match) {
  if (match.empty()) {
    return;
  }

  SetString("matchstr", match.str(0));
  ClearGroupMatchStrings();
  for (std::size_t index = 1; index < match.size() && index <= 9; ++index) {
    SetString("groupmatchstr" + std::to_string(index), match[index].matched ? match.str(index) : "");
  }
}

bool MacroRunner::SetValue(const std::string& target, const MacroValue& value, std::string* error) {
  std::string array_name;
  std::string index_expression;
  if (!ParseArrayReference(target, &array_name, &index_expression)) {
    if (value.is_string) {
      SetString(target, value.string_value);
    } else {
      SetInteger(target, value.int_value);
    }
    return true;
  }

  const int index = EvaluateInteger(index_expression);
  auto string_iter = string_arrays_.find(array_name);
  if (string_iter != string_arrays_.end()) {
    if (index < 0 || static_cast<std::size_t>(index) >= string_iter->second.size()) {
      if (error != nullptr) {
        *error = "string array index is out of range: " + target;
      }
      return false;
    }
    string_iter->second[static_cast<std::size_t>(index)] =
        value.is_string ? value.string_value : std::to_string(value.int_value);
    return true;
  }

  auto int_iter = int_arrays_.find(array_name);
  if (int_iter != int_arrays_.end()) {
    if (index < 0 || static_cast<std::size_t>(index) >= int_iter->second.size()) {
      if (error != nullptr) {
        *error = "integer array index is out of range: " + target;
      }
      return false;
    }
    int_iter->second[static_cast<std::size_t>(index)] =
        value.is_string ? std::atoi(value.string_value.c_str()) : value.int_value;
    return true;
  }

  if (error != nullptr) {
    *error = "array is not defined: " + array_name;
  }
  return false;
}

bool MacroRunner::ResolveArrayValue(const std::string& token, MacroValue* value) const {
  std::string array_name;
  std::string index_expression;
  if (!ParseArrayReference(token, &array_name, &index_expression)) {
    return false;
  }

  const int index = EvaluateInteger(index_expression);
  const auto string_iter = string_arrays_.find(array_name);
  if (string_iter != string_arrays_.end()) {
    if (index < 0 || static_cast<std::size_t>(index) >= string_iter->second.size()) {
      return false;
    }
    if (value != nullptr) {
      value->is_string = true;
      value->string_value = string_iter->second[static_cast<std::size_t>(index)];
      value->int_value = 0;
    }
    return true;
  }

  const auto int_iter = int_arrays_.find(array_name);
  if (int_iter != int_arrays_.end()) {
    if (index < 0 || static_cast<std::size_t>(index) >= int_iter->second.size()) {
      return false;
    }
    if (value != nullptr) {
      value->is_string = false;
      value->int_value = int_iter->second[static_cast<std::size_t>(index)];
      value->string_value.clear();
    }
    return true;
  }

  return false;
}

int MacroRunner::WaitTimeoutMs() const {
  int timeout_ms = 0;
  const auto timeout = variables_.find("timeout");
  if (timeout != variables_.end() && !timeout->second.is_string) {
    timeout_ms += timeout->second.int_value * 1000;
  }
  const auto mtimeout = variables_.find("mtimeout");
  if (mtimeout != variables_.end() && !mtimeout->second.is_string) {
    timeout_ms += mtimeout->second.int_value;
  }
  return std::max(timeout_ms, 0);
}

MacroStepResult MacroRunner::Connect(TerminalSession* session, const std::string& spec) {
  MacroStepResult result;
  const auto profiles = TeraTermIniImporter::ImportHostProfiles(ProfileToIniHost(spec));
  if (profiles.empty()) {
    SetError(&result, "connect host specification could not be parsed");
    return result;
  }

  const ConnectionProfile& profile = profiles[0];
  std::string error;
  if (profile.protocol == ProfileProtocol::Telnet) {
    if (!session->ConnectTelnet(profile.host, profile.port, options_.connect_timeout_ms, profile.proxy, &error)) {
      SetError(&result, error);
    }
  } else if (profile.protocol == ProfileProtocol::Tcp) {
    if (!session->ConnectTcp(profile.host, profile.port, options_.connect_timeout_ms, profile.proxy, &error)) {
      SetError(&result, error);
    }
  } else if (profile.protocol == ProfileProtocol::Ssh) {
    SshConnectionOptions ssh_options;
    ssh_options.host = profile.host;
    ssh_options.port = profile.port == 0 ? 22 : profile.port;
    ssh_options.username = profile.username;
    ssh_options.password = profile.password;
    ssh_options.public_key_path = profile.public_key_path;
    ssh_options.private_key_path = profile.private_key_path;
    ssh_options.passphrase = profile.passphrase;
    ssh_options.proxy = profile.proxy;
    ssh_options.known_hosts_text = options_.known_hosts_text;
    ssh_options.known_host_policy = options_.known_host_policy;
    ssh_options.timeout_ms = options_.connect_timeout_ms;
    SshConnectionResult ssh_result;
    if (!session->ConnectSsh(ssh_options, &ssh_result)) {
      SetError(&result, ssh_result.error);
    } else {
      options_.known_hosts_text = ssh_result.updated_known_hosts_text;
      pending_updated_known_hosts_text_ = options_.known_hosts_text;
      result.updated_known_hosts_text = options_.known_hosts_text;
    }
  } else if (profile.protocol == ProfileProtocol::Serial) {
    SerialOpenOptions serial_options = profile.serial;
    if (serial_options.device_path.empty()) {
      serial_options.device_path = profile.host;
    }
    serial_options.timeout_ms = options_.connect_timeout_ms;
    if (!session->ConnectSerial(serial_options, &error)) {
      SetError(&result, error);
    }
  } else {
    SetError(&result, "connect profile is not supported on HarmonyOS PC yet");
  }
  return result;
}

void MacroRunner::AttachPendingUpdates(MacroStepResult* result) {
  if (!pending_updated_known_hosts_text_.empty()) {
    result->updated_known_hosts_text = pending_updated_known_hosts_text_;
    pending_updated_known_hosts_text_.clear();
  }
  if (!pending_status_box_action_.empty()) {
    result->status_box_action = pending_status_box_action_;
    result->status_box_title = pending_status_box_title_;
    result->status_box_text = pending_status_box_text_;
    pending_status_box_action_.clear();
  }
}

void MacroRunner::SetCompleted(MacroStepResult* result, const std::string& message) {
  done_ = true;
  result->status = MacroStatus::Completed;
  result->message = message;
  result->next_line = next_line_;
}

void MacroRunner::SetError(MacroStepResult* result, const std::string& message) {
  done_ = true;
  result->status = MacroStatus::Error;
  result->message = message;
  result->next_line = next_line_;
}

void MacroRunner::SetWaiting(MacroStepResult* result, const std::string& message, int wait_ms) {
  result->status = MacroStatus::Waiting;
  result->message = message;
  result->wait_ms = wait_ms;
  result->next_line = next_line_;
}

void MacroRunner::AttachDialog(MacroStepResult* result) const {
  result->dialog_type = wait_.dialog_type;
  result->dialog_title = wait_.dialog_title;
  result->dialog_text = wait_.dialog_text;
  result->dialog_default_text = wait_.dialog_default_text;
  result->dialog_items = wait_.dialog_items;
  result->dialog_selected_index = wait_.dialog_selected_index;
  result->dialog_password = wait_.dialog_password;
  result->dialog_save = wait_.dialog_save;
}

std::string MacroStatusName(MacroStatus status) {
  switch (status) {
    case MacroStatus::Running:
      return "running";
    case MacroStatus::Waiting:
      return "waiting";
    case MacroStatus::Completed:
      return "completed";
    case MacroStatus::Error:
      return "error";
  }
  return "error";
}

}  // namespace teraterm::ohos
