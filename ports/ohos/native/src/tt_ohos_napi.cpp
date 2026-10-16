#include "profile_importer.h"
#include "known_hosts_store.h"
#include "macro_runner.h"
#include "terminal_session.h"

#include <napi/native_api.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <mutex>
#include <sys/stat.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using teraterm::ohos::SessionPumpResult;
using teraterm::ohos::TerminalSession;

std::mutex g_sessions_mutex;
std::unordered_map<uint32_t, std::unique_ptr<TerminalSession>> g_sessions;
uint32_t g_next_session_id = 1;

struct MacroHandle {
  uint32_t session_id = 0;
  std::unique_ptr<teraterm::ohos::MacroRunner> runner;
};

std::mutex g_macros_mutex;
std::unordered_map<uint32_t, MacroHandle> g_macros;
uint32_t g_next_macro_id = 1;

std::mutex g_forwards_mutex;
std::unordered_map<uint32_t, std::unique_ptr<teraterm::ohos::SshPortForwardHandle>> g_forwards;
uint32_t g_next_forward_id = 1;

bool LooksLikeSerialDeviceName(const char* name) {
  if (name == nullptr) {
    return false;
  }
  const char* prefixes[] = {
      "ttyS", "ttyUSB", "ttyACM", "ttyAMA", "ttyHS", "ttyHSL", "ttyXRUSB", "ttyO", "cu.",
  };
  for (const char* prefix : prefixes) {
    if (std::strncmp(name, prefix, std::strlen(prefix)) == 0) {
      return true;
    }
  }
  return false;
}

bool IsCharacterDevice(const std::string& path) {
  struct stat st {};
  if (stat(path.c_str(), &st) != 0) {
    return false;
  }
  return S_ISCHR(st.st_mode);
}

std::vector<std::string> ListSerialDevicePaths() {
  std::vector<std::string> devices;
  DIR* dev = opendir("/dev");
  if (dev == nullptr) {
    return devices;
  }

  while (dirent* entry = readdir(dev)) {
    if (!LooksLikeSerialDeviceName(entry->d_name)) {
      continue;
    }
    std::string path = std::string("/dev/") + entry->d_name;
    if (IsCharacterDevice(path)) {
      devices.push_back(path);
    }
  }
  closedir(dev);

  std::sort(devices.begin(), devices.end());
  devices.erase(std::unique(devices.begin(), devices.end()), devices.end());
  return devices;
}

uint32_t ReadUint32(napi_env env, napi_value value, uint32_t fallback) {
  uint32_t result = fallback;
  napi_get_value_uint32(env, value, &result);
  return result;
}

bool ReadBool(napi_env env, napi_value value, bool fallback) {
  bool result = fallback;
  napi_get_value_bool(env, value, &result);
  return result;
}

std::string ReadString(napi_env env, napi_value value) {
  size_t length = 0;
  napi_get_value_string_utf8(env, value, nullptr, 0, &length);
  std::vector<char> buffer(length + 1, '\0');
  napi_get_value_string_utf8(env, value, buffer.data(), buffer.size(), &length);
  return std::string(buffer.data(), length);
}

bool HasNamedProperty(napi_env env, napi_value object, const char* name) {
  bool has_property = false;
  napi_has_named_property(env, object, name, &has_property);
  return has_property;
}

std::string ReadObjectString(napi_env env,
                             napi_value object,
                             const char* name,
                             const std::string& fallback = "") {
  if (!HasNamedProperty(env, object, name)) {
    return fallback;
  }
  napi_value value;
  napi_get_named_property(env, object, name, &value);
  napi_valuetype type = napi_undefined;
  napi_typeof(env, value, &type);
  if (type != napi_string) {
    return fallback;
  }
  return ReadString(env, value);
}

uint32_t ReadObjectUint32(napi_env env, napi_value object, const char* name, uint32_t fallback) {
  if (!HasNamedProperty(env, object, name)) {
    return fallback;
  }
  napi_value value;
  napi_get_named_property(env, object, name, &value);
  napi_valuetype type = napi_undefined;
  napi_typeof(env, value, &type);
  if (type != napi_number) {
    return fallback;
  }
  return ReadUint32(env, value, fallback);
}

teraterm::ohos::ProxyOpenOptions ReadProxyOptions(napi_env env, napi_value value) {
  teraterm::ohos::ProxyOpenOptions proxy;
  napi_valuetype type = napi_undefined;
  napi_typeof(env, value, &type);
  if (type != napi_object) {
    return proxy;
  }

  proxy.type = teraterm::ohos::ProxyTypeFromString(ReadObjectString(env, value, "type", "none"));
  if (proxy.type == teraterm::ohos::ProxyType::None) {
    return proxy;
  }
  proxy.host = ReadObjectString(env, value, "host");
  proxy.port = static_cast<uint16_t>(ReadObjectUint32(env, value, "port", 0));
  proxy.username = ReadObjectString(env, value, "username");
  proxy.password = ReadObjectString(env, value, "password");
  proxy.telnet_hostname_prompt = ReadObjectString(env, value, "telnetHostnamePrompt", proxy.telnet_hostname_prompt);
  proxy.telnet_username_prompt = ReadObjectString(env, value, "telnetUsernamePrompt", proxy.telnet_username_prompt);
  proxy.telnet_password_prompt = ReadObjectString(env, value, "telnetPasswordPrompt", proxy.telnet_password_prompt);
  proxy.telnet_connected_message = ReadObjectString(env, value, "telnetConnectedMessage", proxy.telnet_connected_message);
  proxy.telnet_error_message = ReadObjectString(env, value, "telnetErrorMessage", proxy.telnet_error_message);
  return proxy;
}

teraterm::ohos::SerialOpenOptions ReadSerialOptions(napi_env env, napi_value value) {
  teraterm::ohos::SerialOpenOptions options;
  napi_valuetype type = napi_undefined;
  napi_typeof(env, value, &type);
  if (type != napi_object) {
    return options;
  }

  options.device_path = ReadObjectString(env, value, "devicePath");
  options.baud_rate = ReadObjectUint32(env, value, "baudRate", 9600);
  options.data_bits = static_cast<uint8_t>(ReadObjectUint32(env, value, "dataBits", 8));
  options.stop_bits = static_cast<uint8_t>(ReadObjectUint32(env, value, "stopBits", 1));
  options.parity = teraterm::ohos::SerialParityFromString(ReadObjectString(env, value, "parity", "none"));
  options.flow_control = teraterm::ohos::SerialFlowControlFromString(
      ReadObjectString(env, value, "flowControl", "none"));
  options.timeout_ms = static_cast<int>(ReadObjectUint32(env, value, "timeoutMs", 5000));
  return options;
}

teraterm::ohos::SshConnectionOptions ReadSshConnectionOptions(napi_env env, napi_value object) {
  teraterm::ohos::SshConnectionOptions options;
  options.host = ReadObjectString(env, object, "host");
  options.port = static_cast<uint16_t>(ReadObjectUint32(env, object, "port", 22));
  options.username = ReadObjectString(env, object, "username");
  options.password = ReadObjectString(env, object, "password");
  options.public_key_path = ReadObjectString(env, object, "publicKeyPath");
  options.private_key_path = ReadObjectString(env, object, "privateKeyPath");
  options.passphrase = ReadObjectString(env, object, "passphrase");
  options.known_hosts_text = ReadObjectString(env, object, "knownHostsText");
  options.known_host_policy = teraterm::ohos::KnownHostPolicyFromString(
      ReadObjectString(env, object, "knownHostPolicy", "strict"));
  options.terminal_type = ReadObjectString(env, object, "terminalType", "xterm-256color");
  options.timeout_ms = static_cast<int>(ReadObjectUint32(env, object, "timeoutMs", 5000));
  if (HasNamedProperty(env, object, "proxy")) {
    napi_value proxy_value;
    napi_get_named_property(env, object, "proxy", &proxy_value);
    options.proxy = ReadProxyOptions(env, proxy_value);
  }
  return options;
}

teraterm::ohos::SshFileTransferOptions ReadSshFileTransferOptions(napi_env env, napi_value object) {
  teraterm::ohos::SshFileTransferOptions options;
  options.connection = ReadSshConnectionOptions(env, object);
  options.local_path = ReadObjectString(env, object, "localPath");
  options.remote_path = ReadObjectString(env, object, "remotePath");
  options.mode = static_cast<int>(ReadObjectUint32(env, object, "mode", 0644));
  return options;
}

teraterm::ohos::SshPortForwardOptions ReadSshPortForwardOptions(napi_env env, napi_value object) {
  teraterm::ohos::SshPortForwardOptions options;
  options.connection = ReadSshConnectionOptions(env, object);
  options.local_host = ReadObjectString(env, object, "localHost", "127.0.0.1");
  options.local_port = static_cast<uint16_t>(ReadObjectUint32(env, object, "localPort", 0));
  options.remote_host = ReadObjectString(env, object, "remoteHost");
  options.remote_port = static_cast<uint16_t>(ReadObjectUint32(env, object, "remotePort", 0));
  return options;
}

napi_value MakeUndefined(napi_env env) {
  napi_value result;
  napi_get_undefined(env, &result);
  return result;
}

napi_value MakeError(napi_env env, const char* message) {
  napi_throw_error(env, nullptr, message);
  return MakeUndefined(env);
}

bool IsMacroActive(const teraterm::ohos::MacroStepResult& result) {
  return result.status == teraterm::ohos::MacroStatus::Running ||
         result.status == teraterm::ohos::MacroStatus::Waiting;
}

napi_value MakePumpResult(napi_env env, const SessionPumpResult& pump_result) {
  napi_value result;
  napi_create_object(env, &result);

  napi_value bytes_read;
  napi_create_uint32(env, static_cast<uint32_t>(pump_result.bytes_read), &bytes_read);
  napi_value connected;
  napi_get_boolean(env, pump_result.connected, &connected);
  napi_value error;
  napi_create_string_utf8(env, pump_result.error.c_str(), pump_result.error.size(), &error);

  napi_set_named_property(env, result, "bytesRead", bytes_read);
  napi_set_named_property(env, result, "connected", connected);
  napi_set_named_property(env, result, "error", error);
  return result;
}

napi_value MakeCell(napi_env env, const teraterm::ohos::TerminalCell& cell) {
  napi_value result;
  napi_create_object(env, &result);

  napi_value text;
  napi_create_string_utf8(env, cell.text.c_str(), cell.text.size(), &text);
  napi_value foreground;
  napi_create_int32(env, cell.style.foreground, &foreground);
  napi_value background;
  napi_create_int32(env, cell.style.background, &background);
  napi_value bold;
  napi_get_boolean(env, cell.style.bold, &bold);
  napi_value underline;
  napi_get_boolean(env, cell.style.underline, &underline);
  napi_value inverse;
  napi_get_boolean(env, cell.style.inverse, &inverse);

  napi_set_named_property(env, result, "text", text);
  napi_value columns;
  napi_create_uint32(env, cell.columns, &columns);
  napi_set_named_property(env, result, "columns", columns);
  napi_set_named_property(env, result, "foreground", foreground);
  napi_set_named_property(env, result, "background", background);
  napi_set_named_property(env, result, "bold", bold);
  napi_set_named_property(env, result, "underline", underline);
  napi_set_named_property(env, result, "inverse", inverse);
  return result;
}

napi_value MakeStringArray(napi_env env, const std::vector<std::string>& values) {
  napi_value result;
  napi_create_array_with_length(env, values.size(), &result);
  for (uint32_t index = 0; index < values.size(); ++index) {
    napi_value value;
    napi_create_string_utf8(env, values[index].c_str(), values[index].size(), &value);
    napi_set_element(env, result, index, value);
  }
  return result;
}

napi_value MakeKnownHostEntry(napi_env env, const teraterm::ohos::KnownHostEntry& entry) {
  napi_value result;
  napi_create_object(env, &result);

  napi_value marker;
  napi_create_string_utf8(env, entry.marker.c_str(), entry.marker.size(), &marker);
  napi_value key_type;
  napi_create_string_utf8(env, entry.key_type.c_str(), entry.key_type.size(), &key_type);
  napi_value key_base64;
  napi_create_string_utf8(env, entry.key_base64.c_str(), entry.key_base64.size(), &key_base64);
  napi_value comment;
  napi_create_string_utf8(env, entry.comment.c_str(), entry.comment.size(), &comment);
  napi_value raw_line;
  napi_create_string_utf8(env, entry.raw_line.c_str(), entry.raw_line.size(), &raw_line);

  napi_set_named_property(env, result, "marker", marker);
  napi_set_named_property(env, result, "hosts", MakeStringArray(env, entry.hosts));
  napi_set_named_property(env, result, "keyType", key_type);
  napi_set_named_property(env, result, "keyBase64", key_base64);
  napi_set_named_property(env, result, "comment", comment);
  napi_set_named_property(env, result, "rawLine", raw_line);
  napi_set_named_property(env, result, "warnings", MakeStringArray(env, entry.warnings));
  return result;
}

napi_value MakeKnownHostMatch(napi_env env, const teraterm::ohos::KnownHostMatch& match) {
  napi_value result;
  napi_create_object(env, &result);

  napi_value status;
  const std::string status_name = teraterm::ohos::KnownHostMatchStatusName(match.status);
  napi_create_string_utf8(env, status_name.c_str(), status_name.size(), &status);
  napi_value message;
  napi_create_string_utf8(env, match.message.c_str(), match.message.size(), &message);

  napi_set_named_property(env, result, "status", status);
  napi_set_named_property(env, result, "message", message);
  napi_set_named_property(env, result, "entry", MakeKnownHostEntry(env, match.entry));
  return result;
}

napi_value MakeSshConnectionResult(napi_env env, const teraterm::ohos::SshConnectionResult& ssh_result) {
  napi_value result;
  napi_create_object(env, &result);

  napi_value connected;
  napi_get_boolean(env, ssh_result.connected, &connected);
  napi_value error;
  napi_create_string_utf8(env, ssh_result.error.c_str(), ssh_result.error.size(), &error);
  napi_value key_type;
  napi_create_string_utf8(env, ssh_result.key_type.c_str(), ssh_result.key_type.size(), &key_type);
  napi_value key_base64;
  napi_create_string_utf8(env, ssh_result.key_base64.c_str(), ssh_result.key_base64.size(), &key_base64);
  napi_value updated_known_hosts;
  napi_create_string_utf8(env, ssh_result.updated_known_hosts_text.c_str(),
                          ssh_result.updated_known_hosts_text.size(), &updated_known_hosts);

  napi_set_named_property(env, result, "connected", connected);
  napi_set_named_property(env, result, "error", error);
  napi_set_named_property(env, result, "keyType", key_type);
  napi_set_named_property(env, result, "keyBase64", key_base64);
  napi_set_named_property(env, result, "updatedKnownHostsText", updated_known_hosts);
  napi_set_named_property(env, result, "hostKeyMatch", MakeKnownHostMatch(env, ssh_result.host_key_match));
  return result;
}

napi_value MakeSshFileTransferResult(napi_env env, const teraterm::ohos::SshFileTransferResult& transfer_result) {
  napi_value result;
  napi_create_object(env, &result);

  napi_value success;
  napi_get_boolean(env, transfer_result.success, &success);
  napi_value error;
  napi_create_string_utf8(env, transfer_result.error.c_str(), transfer_result.error.size(), &error);
  napi_value bytes;
  napi_create_uint32(env, static_cast<uint32_t>(transfer_result.bytes_transferred), &bytes);
  napi_value updated_known_hosts;
  napi_create_string_utf8(env, transfer_result.updated_known_hosts_text.c_str(),
                          transfer_result.updated_known_hosts_text.size(), &updated_known_hosts);

  napi_set_named_property(env, result, "success", success);
  napi_set_named_property(env, result, "error", error);
  napi_set_named_property(env, result, "bytesTransferred", bytes);
  napi_set_named_property(env, result, "updatedKnownHostsText", updated_known_hosts);
  napi_set_named_property(env, result, "hostKeyMatch", MakeKnownHostMatch(env, transfer_result.host_key_match));
  return result;
}

napi_value MakeSshPortForwardStartResult(napi_env env,
                                          uint32_t forward_id,
                                          const teraterm::ohos::SshPortForwardStartResult& forward_result) {
  napi_value result;
  napi_create_object(env, &result);

  napi_value started;
  napi_get_boolean(env, forward_result.started, &started);
  napi_value error;
  napi_create_string_utf8(env, forward_result.error.c_str(), forward_result.error.size(), &error);
  napi_value id;
  napi_create_uint32(env, forward_id, &id);
  napi_value bound_port;
  napi_create_uint32(env, forward_result.bound_port, &bound_port);
  napi_value updated_known_hosts;
  napi_create_string_utf8(env, forward_result.updated_known_hosts_text.c_str(),
                          forward_result.updated_known_hosts_text.size(), &updated_known_hosts);

  napi_set_named_property(env, result, "started", started);
  napi_set_named_property(env, result, "error", error);
  napi_set_named_property(env, result, "forwardId", id);
  napi_set_named_property(env, result, "boundPort", bound_port);
  napi_set_named_property(env, result, "updatedKnownHostsText", updated_known_hosts);
  napi_set_named_property(env, result, "hostKeyMatch", MakeKnownHostMatch(env, forward_result.host_key_match));
  return result;
}

napi_value MakeMacroResult(napi_env env,
                           uint32_t macro_id,
                           const teraterm::ohos::MacroStepResult& macro_result) {
  napi_value result;
  napi_create_object(env, &result);

  napi_value id;
  napi_create_uint32(env, macro_id, &id);
  napi_value status;
  const std::string status_name = teraterm::ohos::MacroStatusName(macro_result.status);
  napi_create_string_utf8(env, status_name.c_str(), status_name.size(), &status);
  napi_value next_line;
  napi_create_uint32(env, static_cast<uint32_t>(macro_result.next_line), &next_line);
  napi_value wait_ms;
  napi_create_int32(env, macro_result.wait_ms, &wait_ms);
  napi_value message;
  napi_create_string_utf8(env, macro_result.message.c_str(), macro_result.message.size(), &message);
  napi_value updated_known_hosts;
  napi_create_string_utf8(env, macro_result.updated_known_hosts_text.c_str(),
                          macro_result.updated_known_hosts_text.size(), &updated_known_hosts);
  napi_value dialog_type;
  napi_create_string_utf8(env, macro_result.dialog_type.c_str(), macro_result.dialog_type.size(), &dialog_type);
  napi_value dialog_title;
  napi_create_string_utf8(env, macro_result.dialog_title.c_str(), macro_result.dialog_title.size(), &dialog_title);
  napi_value dialog_text;
  napi_create_string_utf8(env, macro_result.dialog_text.c_str(), macro_result.dialog_text.size(), &dialog_text);
  napi_value dialog_default_text;
  napi_create_string_utf8(env, macro_result.dialog_default_text.c_str(),
                          macro_result.dialog_default_text.size(), &dialog_default_text);
  napi_value dialog_selected_index;
  napi_create_int32(env, macro_result.dialog_selected_index, &dialog_selected_index);
  napi_value dialog_password;
  napi_get_boolean(env, macro_result.dialog_password, &dialog_password);
  napi_value dialog_save;
  napi_get_boolean(env, macro_result.dialog_save, &dialog_save);
  napi_value status_box_action;
  napi_create_string_utf8(env, macro_result.status_box_action.c_str(),
                          macro_result.status_box_action.size(), &status_box_action);
  napi_value status_box_title;
  napi_create_string_utf8(env, macro_result.status_box_title.c_str(),
                          macro_result.status_box_title.size(), &status_box_title);
  napi_value status_box_text;
  napi_create_string_utf8(env, macro_result.status_box_text.c_str(),
                          macro_result.status_box_text.size(), &status_box_text);

  napi_set_named_property(env, result, "macroId", id);
  napi_set_named_property(env, result, "status", status);
  napi_set_named_property(env, result, "nextLine", next_line);
  napi_set_named_property(env, result, "waitMs", wait_ms);
  napi_set_named_property(env, result, "message", message);
  napi_set_named_property(env, result, "updatedKnownHostsText", updated_known_hosts);
  napi_set_named_property(env, result, "dialogType", dialog_type);
  napi_set_named_property(env, result, "dialogTitle", dialog_title);
  napi_set_named_property(env, result, "dialogText", dialog_text);
  napi_set_named_property(env, result, "dialogDefaultText", dialog_default_text);
  napi_set_named_property(env, result, "dialogItems", MakeStringArray(env, macro_result.dialog_items));
  napi_set_named_property(env, result, "dialogSelectedIndex", dialog_selected_index);
  napi_set_named_property(env, result, "dialogPassword", dialog_password);
  napi_set_named_property(env, result, "dialogSave", dialog_save);
  napi_set_named_property(env, result, "statusBoxAction", status_box_action);
  napi_set_named_property(env, result, "statusBoxTitle", status_box_title);
  napi_set_named_property(env, result, "statusBoxText", status_box_text);
  return result;
}

napi_value MakeProfile(napi_env env, const teraterm::ohos::ConnectionProfile& profile) {
  napi_value result;
  napi_create_object(env, &result);

  napi_value name;
  napi_create_string_utf8(env, profile.name.c_str(), profile.name.size(), &name);
  napi_value protocol;
  const std::string protocol_name = teraterm::ohos::ProfileProtocolName(profile.protocol);
  napi_create_string_utf8(env, protocol_name.c_str(), protocol_name.size(), &protocol);
  napi_value host;
  napi_create_string_utf8(env, profile.host.c_str(), profile.host.size(), &host);
  napi_value port;
  napi_create_uint32(env, profile.port, &port);
  napi_value username;
  napi_create_string_utf8(env, profile.username.c_str(), profile.username.size(), &username);
  napi_value password;
  napi_create_string_utf8(env, profile.password.c_str(), profile.password.size(), &password);
  napi_value public_key_path;
  napi_create_string_utf8(env, profile.public_key_path.c_str(), profile.public_key_path.size(), &public_key_path);
  napi_value private_key_path;
  napi_create_string_utf8(env, profile.private_key_path.c_str(), profile.private_key_path.size(), &private_key_path);
  napi_value passphrase;
  napi_create_string_utf8(env, profile.passphrase.c_str(), profile.passphrase.size(), &passphrase);
  napi_value proxy_type;
  const std::string proxy_type_name = teraterm::ohos::ProxyTypeName(profile.proxy.type);
  napi_create_string_utf8(env, proxy_type_name.c_str(), proxy_type_name.size(), &proxy_type);
  napi_value proxy_host;
  napi_create_string_utf8(env, profile.proxy.host.c_str(), profile.proxy.host.size(), &proxy_host);
  napi_value proxy_port;
  napi_create_uint32(env, profile.proxy.port, &proxy_port);
  napi_value proxy_username;
  napi_create_string_utf8(env, profile.proxy.username.c_str(), profile.proxy.username.size(), &proxy_username);
  napi_value proxy_password;
  napi_create_string_utf8(env, profile.proxy.password.c_str(), profile.proxy.password.size(), &proxy_password);
  napi_value serial_device_path;
  napi_create_string_utf8(env, profile.serial.device_path.c_str(), profile.serial.device_path.size(), &serial_device_path);
  napi_value baud_rate;
  napi_create_uint32(env, profile.serial.baud_rate, &baud_rate);
  napi_value data_bits;
  napi_create_uint32(env, profile.serial.data_bits, &data_bits);
  napi_value stop_bits;
  napi_create_uint32(env, profile.serial.stop_bits, &stop_bits);
  napi_value parity;
  const std::string parity_name = teraterm::ohos::SerialParityName(profile.serial.parity);
  napi_create_string_utf8(env, parity_name.c_str(), parity_name.size(), &parity);
  napi_value flow_control;
  const std::string flow_control_name = teraterm::ohos::SerialFlowControlName(profile.serial.flow_control);
  napi_create_string_utf8(env, flow_control_name.c_str(), flow_control_name.size(), &flow_control);
  napi_value setup_file;
  napi_create_string_utf8(env, profile.setup_file.c_str(), profile.setup_file.size(), &setup_file);
  napi_value macro_file;
  napi_create_string_utf8(env, profile.macro_file.c_str(), profile.macro_file.size(), &macro_file);
  napi_value source;
  napi_create_string_utf8(env, profile.source.c_str(), profile.source.size(), &source);

  napi_set_named_property(env, result, "name", name);
  napi_set_named_property(env, result, "protocol", protocol);
  napi_set_named_property(env, result, "host", host);
  napi_set_named_property(env, result, "port", port);
  napi_set_named_property(env, result, "username", username);
  napi_set_named_property(env, result, "password", password);
  napi_set_named_property(env, result, "publicKeyPath", public_key_path);
  napi_set_named_property(env, result, "privateKeyPath", private_key_path);
  napi_set_named_property(env, result, "passphrase", passphrase);
  napi_set_named_property(env, result, "proxyType", proxy_type);
  napi_set_named_property(env, result, "proxyHost", proxy_host);
  napi_set_named_property(env, result, "proxyPort", proxy_port);
  napi_set_named_property(env, result, "proxyUsername", proxy_username);
  napi_set_named_property(env, result, "proxyPassword", proxy_password);
  napi_set_named_property(env, result, "serialDevicePath", serial_device_path);
  napi_set_named_property(env, result, "baudRate", baud_rate);
  napi_set_named_property(env, result, "dataBits", data_bits);
  napi_set_named_property(env, result, "stopBits", stop_bits);
  napi_set_named_property(env, result, "parity", parity);
  napi_set_named_property(env, result, "flowControl", flow_control);
  napi_set_named_property(env, result, "setupFile", setup_file);
  napi_set_named_property(env, result, "macroFile", macro_file);
  napi_set_named_property(env, result, "source", source);
  napi_set_named_property(env, result, "warnings", MakeStringArray(env, profile.warnings));
  return result;
}

TerminalSession* FindSession(uint32_t id) {
  auto iter = g_sessions.find(id);
  return iter == g_sessions.end() ? nullptr : iter->second.get();
}

napi_value CreateSession(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

  const uint32_t rows = argc > 0 ? ReadUint32(env, args[0], 24) : 24;
  const uint32_t cols = argc > 1 ? ReadUint32(env, args[1], 80) : 80;

  std::lock_guard<std::mutex> lock(g_sessions_mutex);
  const uint32_t id = g_next_session_id++;
  g_sessions.emplace(id, std::make_unique<TerminalSession>(rows, cols));

  napi_value result;
  napi_create_uint32(env, id, &result);
  return result;
}

napi_value WriteSession(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 2) {
    return MakeError(env, "writeSession requires id and text");
  }

  const uint32_t id = ReadUint32(env, args[0], 0);
  const std::string text = ReadString(env, args[1]);

  std::lock_guard<std::mutex> lock(g_sessions_mutex);
  TerminalSession* session = FindSession(id);
  if (session == nullptr) {
    return MakeError(env, "unknown session id");
  }
  session->WriteLocalOutput(text);
  return MakeUndefined(env);
}

napi_value ConnectTcpSession(napi_env env, napi_callback_info info) {
  size_t argc = 4;
  napi_value args[4];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 3) {
    return MakeError(env, "connectTcpSession requires id, host, and port");
  }

  const uint32_t id = ReadUint32(env, args[0], 0);
  const std::string host = ReadString(env, args[1]);
  const uint32_t port = ReadUint32(env, args[2], 0);
  const uint32_t timeout_ms = argc > 3 ? ReadUint32(env, args[3], 5000) : 5000;

  std::lock_guard<std::mutex> lock(g_sessions_mutex);
  TerminalSession* session = FindSession(id);
  if (session == nullptr) {
    return MakeError(env, "unknown session id");
  }

  std::string error;
  const bool connected = session->ConnectTcp(host, static_cast<uint16_t>(port), timeout_ms, &error);
  if (!connected) {
    return MakeError(env, error.c_str());
  }
  return MakeUndefined(env);
}

napi_value ConnectTcpSessionWithProxy(napi_env env, napi_callback_info info) {
  size_t argc = 5;
  napi_value args[5];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 4) {
    return MakeError(env, "connectTcpSessionWithProxy requires id, host, port, and proxy options");
  }

  const uint32_t id = ReadUint32(env, args[0], 0);
  const std::string host = ReadString(env, args[1]);
  const uint32_t port = ReadUint32(env, args[2], 0);
  const teraterm::ohos::ProxyOpenOptions proxy = ReadProxyOptions(env, args[3]);
  const uint32_t timeout_ms = argc > 4 ? ReadUint32(env, args[4], 5000) : 5000;

  std::lock_guard<std::mutex> lock(g_sessions_mutex);
  TerminalSession* session = FindSession(id);
  if (session == nullptr) {
    return MakeError(env, "unknown session id");
  }

  std::string error;
  const bool connected = session->ConnectTcp(host, static_cast<uint16_t>(port), timeout_ms, proxy, &error);
  if (!connected) {
    return MakeError(env, error.c_str());
  }
  return MakeUndefined(env);
}

napi_value ConnectTelnetSession(napi_env env, napi_callback_info info) {
  size_t argc = 4;
  napi_value args[4];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 3) {
    return MakeError(env, "connectTelnetSession requires id, host, and port");
  }

  const uint32_t id = ReadUint32(env, args[0], 0);
  const std::string host = ReadString(env, args[1]);
  const uint32_t port = ReadUint32(env, args[2], 23);
  const uint32_t timeout_ms = argc > 3 ? ReadUint32(env, args[3], 5000) : 5000;

  std::lock_guard<std::mutex> lock(g_sessions_mutex);
  TerminalSession* session = FindSession(id);
  if (session == nullptr) {
    return MakeError(env, "unknown session id");
  }

  std::string error;
  const bool connected = session->ConnectTelnet(host, static_cast<uint16_t>(port), timeout_ms, &error);
  if (!connected) {
    return MakeError(env, error.c_str());
  }
  return MakeUndefined(env);
}

napi_value ConnectTelnetSessionWithProxy(napi_env env, napi_callback_info info) {
  size_t argc = 5;
  napi_value args[5];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 4) {
    return MakeError(env, "connectTelnetSessionWithProxy requires id, host, port, and proxy options");
  }

  const uint32_t id = ReadUint32(env, args[0], 0);
  const std::string host = ReadString(env, args[1]);
  const uint32_t port = ReadUint32(env, args[2], 23);
  const teraterm::ohos::ProxyOpenOptions proxy = ReadProxyOptions(env, args[3]);
  const uint32_t timeout_ms = argc > 4 ? ReadUint32(env, args[4], 5000) : 5000;

  std::lock_guard<std::mutex> lock(g_sessions_mutex);
  TerminalSession* session = FindSession(id);
  if (session == nullptr) {
    return MakeError(env, "unknown session id");
  }

  std::string error;
  const bool connected = session->ConnectTelnet(host, static_cast<uint16_t>(port), timeout_ms, proxy, &error);
  if (!connected) {
    return MakeError(env, error.c_str());
  }
  return MakeUndefined(env);
}

napi_value ConnectSerialSession(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 2) {
    return MakeError(env, "connectSerialSession requires id and serial options");
  }

  const uint32_t id = ReadUint32(env, args[0], 0);
  const teraterm::ohos::SerialOpenOptions options = ReadSerialOptions(env, args[1]);
  if (options.device_path.empty()) {
    return MakeError(env, "serial device path is required");
  }

  std::lock_guard<std::mutex> lock(g_sessions_mutex);
  TerminalSession* session = FindSession(id);
  if (session == nullptr) {
    return MakeError(env, "unknown session id");
  }

  std::string error;
  const bool connected = session->ConnectSerial(options, &error);
  if (!connected) {
    return MakeError(env, error.c_str());
  }
  return MakeUndefined(env);
}

napi_value ListSerialDevices(napi_env env, napi_callback_info /*info*/) {
  return MakeStringArray(env, ListSerialDevicePaths());
}

napi_value ConnectSshSession(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 2) {
    return MakeError(env, "connectSshSession requires id and options");
  }

  const uint32_t id = ReadUint32(env, args[0], 0);
  napi_valuetype options_type = napi_undefined;
  napi_typeof(env, args[1], &options_type);
  if (options_type != napi_object) {
    return MakeError(env, "connectSshSession options must be an object");
  }

  teraterm::ohos::SshConnectionOptions options = ReadSshConnectionOptions(env, args[1]);

  if (options.host.empty() || options.username.empty()) {
    return MakeError(env, "SSH host and username are required");
  }

  std::lock_guard<std::mutex> lock(g_sessions_mutex);
  TerminalSession* session = FindSession(id);
  if (session == nullptr) {
    return MakeError(env, "unknown session id");
  }

  teraterm::ohos::SshConnectionResult ssh_result;
  session->ConnectSsh(options, &ssh_result);
  return MakeSshConnectionResult(env, ssh_result);
}

napi_value ScpDownload(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 1) {
    return MakeError(env, "scpDownload requires options");
  }

  napi_valuetype options_type = napi_undefined;
  napi_typeof(env, args[0], &options_type);
  if (options_type != napi_object) {
    return MakeError(env, "scpDownload options must be an object");
  }

  const teraterm::ohos::SshFileTransferOptions options = ReadSshFileTransferOptions(env, args[0]);
  if (options.connection.host.empty() || options.connection.username.empty() ||
      options.local_path.empty() || options.remote_path.empty()) {
    return MakeError(env, "SCP host, username, local path, and remote path are required");
  }
  return MakeSshFileTransferResult(env, teraterm::ohos::DownloadScpFile(options));
}

napi_value ScpUpload(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 1) {
    return MakeError(env, "scpUpload requires options");
  }

  napi_valuetype options_type = napi_undefined;
  napi_typeof(env, args[0], &options_type);
  if (options_type != napi_object) {
    return MakeError(env, "scpUpload options must be an object");
  }

  const teraterm::ohos::SshFileTransferOptions options = ReadSshFileTransferOptions(env, args[0]);
  if (options.connection.host.empty() || options.connection.username.empty() ||
      options.local_path.empty() || options.remote_path.empty()) {
    return MakeError(env, "SCP host, username, local path, and remote path are required");
  }
  return MakeSshFileTransferResult(env, teraterm::ohos::UploadScpFile(options));
}

napi_value SftpDownload(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 1) {
    return MakeError(env, "sftpDownload requires options");
  }

  napi_valuetype options_type = napi_undefined;
  napi_typeof(env, args[0], &options_type);
  if (options_type != napi_object) {
    return MakeError(env, "sftpDownload options must be an object");
  }

  const teraterm::ohos::SshFileTransferOptions options = ReadSshFileTransferOptions(env, args[0]);
  if (options.connection.host.empty() || options.connection.username.empty() ||
      options.local_path.empty() || options.remote_path.empty()) {
    return MakeError(env, "SFTP host, username, local path, and remote path are required");
  }
  return MakeSshFileTransferResult(env, teraterm::ohos::DownloadSftpFile(options));
}

napi_value SftpUpload(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 1) {
    return MakeError(env, "sftpUpload requires options");
  }

  napi_valuetype options_type = napi_undefined;
  napi_typeof(env, args[0], &options_type);
  if (options_type != napi_object) {
    return MakeError(env, "sftpUpload options must be an object");
  }

  const teraterm::ohos::SshFileTransferOptions options = ReadSshFileTransferOptions(env, args[0]);
  if (options.connection.host.empty() || options.connection.username.empty() ||
      options.local_path.empty() || options.remote_path.empty()) {
    return MakeError(env, "SFTP host, username, local path, and remote path are required");
  }
  return MakeSshFileTransferResult(env, teraterm::ohos::UploadSftpFile(options));
}

napi_value StartSshLocalForward(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 1) {
    return MakeError(env, "startSshLocalForward requires options");
  }

  napi_valuetype options_type = napi_undefined;
  napi_typeof(env, args[0], &options_type);
  if (options_type != napi_object) {
    return MakeError(env, "startSshLocalForward options must be an object");
  }

  const teraterm::ohos::SshPortForwardOptions options = ReadSshPortForwardOptions(env, args[0]);
  if (options.connection.host.empty() || options.connection.username.empty() ||
      options.remote_host.empty() || options.remote_port == 0) {
    return MakeError(env, "SSH forward host, username, remote host, and remote port are required");
  }

  teraterm::ohos::SshPortForwardStartResult forward_result;
  auto handle = teraterm::ohos::StartSshLocalPortForward(options, &forward_result);
  uint32_t forward_id = 0;
  if (handle != nullptr && forward_result.started) {
    std::lock_guard<std::mutex> lock(g_forwards_mutex);
    forward_id = g_next_forward_id++;
    g_forwards[forward_id] = std::move(handle);
  }
  return MakeSshPortForwardStartResult(env, forward_id, forward_result);
}

napi_value StopSshLocalForward(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 1) {
    return MakeError(env, "stopSshLocalForward requires forward id");
  }

  const uint32_t forward_id = ReadUint32(env, args[0], 0);
  std::unique_ptr<teraterm::ohos::SshPortForwardHandle> handle;
  {
    std::lock_guard<std::mutex> lock(g_forwards_mutex);
    auto iter = g_forwards.find(forward_id);
    if (iter == g_forwards.end()) {
      return MakeError(env, "unknown SSH forward id");
    }
    handle = std::move(iter->second);
    g_forwards.erase(iter);
  }
  handle->Stop();
  return MakeUndefined(env);
}

napi_value SendInput(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 2) {
    return MakeError(env, "sendInput requires id and text");
  }

  const uint32_t id = ReadUint32(env, args[0], 0);
  const std::string text = ReadString(env, args[1]);

  std::lock_guard<std::mutex> lock(g_sessions_mutex);
  TerminalSession* session = FindSession(id);
  if (session == nullptr) {
    return MakeError(env, "unknown session id");
  }

  std::string error;
  if (!session->SendInput(text, &error)) {
    return MakeError(env, error.c_str());
  }
  return MakeUndefined(env);
}

napi_value PumpSession(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 1) {
    return MakeError(env, "pumpSession requires id");
  }

  const uint32_t id = ReadUint32(env, args[0], 0);
  const uint32_t max_bytes = argc > 1 ? ReadUint32(env, args[1], 65536) : 65536;

  std::lock_guard<std::mutex> lock(g_sessions_mutex);
  TerminalSession* session = FindSession(id);
  if (session == nullptr) {
    return MakeError(env, "unknown session id");
  }
  return MakePumpResult(env, session->Pump(max_bytes));
}

napi_value DisconnectSession(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 1) {
    return MakeError(env, "disconnectSession requires id");
  }

  const uint32_t id = ReadUint32(env, args[0], 0);
  std::lock_guard<std::mutex> lock(g_sessions_mutex);
  TerminalSession* session = FindSession(id);
  if (session == nullptr) {
    return MakeError(env, "unknown session id");
  }
  session->Disconnect();
  return MakeUndefined(env);
}

napi_value DrainTranscript(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 1) {
    return MakeError(env, "drainTranscript requires id");
  }

  const uint32_t id = ReadUint32(env, args[0], 0);
  std::lock_guard<std::mutex> lock(g_sessions_mutex);
  TerminalSession* session = FindSession(id);
  if (session == nullptr) {
    return MakeError(env, "unknown session id");
  }

  const std::string transcript = session->DrainTranscript();
  napi_value result;
  napi_create_string_utf8(env, transcript.c_str(), transcript.size(), &result);
  return result;
}

napi_value ReadOutputHistory(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 1) {
    return MakeError(env, "readOutputHistory requires id");
  }

  const uint32_t id = ReadUint32(env, args[0], 0);
  std::lock_guard<std::mutex> lock(g_sessions_mutex);
  TerminalSession* session = FindSession(id);
  if (session == nullptr) {
    return MakeError(env, "unknown session id");
  }

  const std::string& history = session->OutputHistory();
  napi_value result;
  napi_create_string_utf8(env, history.c_str(), history.size(), &result);
  return result;
}

napi_value StartMacro(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value args[3];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 2) {
    return MakeError(env, "startMacro requires session id and script");
  }

  const uint32_t session_id = ReadUint32(env, args[0], 0);
  const std::string script = ReadString(env, args[1]);

  teraterm::ohos::MacroOptions options;
  if (argc > 2) {
    napi_valuetype options_type = napi_undefined;
    napi_typeof(env, args[2], &options_type);
    if (options_type == napi_object) {
      options.known_hosts_text = ReadObjectString(env, args[2], "knownHostsText");
      options.known_host_policy = teraterm::ohos::KnownHostPolicyFromString(
          ReadObjectString(env, args[2], "knownHostPolicy", "accept_new"));
      options.connect_timeout_ms = static_cast<int>(ReadObjectUint32(env, args[2], "connectTimeoutMs", 5000));
    }
  }

  std::lock_guard<std::mutex> session_lock(g_sessions_mutex);
  TerminalSession* session = FindSession(session_id);
  if (session == nullptr) {
    return MakeError(env, "unknown session id");
  }

  auto runner = std::make_unique<teraterm::ohos::MacroRunner>(script, options);
  const teraterm::ohos::MacroStepResult macro_result = runner->Advance(session);

  uint32_t macro_id = 0;
  if (!IsMacroActive(macro_result)) {
    return MakeMacroResult(env, macro_id, macro_result);
  }

  std::lock_guard<std::mutex> macro_lock(g_macros_mutex);
  macro_id = g_next_macro_id++;
  g_macros[macro_id] = MacroHandle{session_id, std::move(runner)};
  return MakeMacroResult(env, macro_id, macro_result);
}

napi_value PumpMacro(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 1) {
    return MakeError(env, "pumpMacro requires macro id");
  }

  const uint32_t macro_id = ReadUint32(env, args[0], 0);
  std::lock_guard<std::mutex> session_lock(g_sessions_mutex);
  std::lock_guard<std::mutex> macro_lock(g_macros_mutex);
  auto macro_iter = g_macros.find(macro_id);
  if (macro_iter == g_macros.end()) {
    return MakeError(env, "unknown macro id");
  }
  TerminalSession* session = FindSession(macro_iter->second.session_id);
  if (session == nullptr) {
    return MakeError(env, "macro session is missing");
  }

  const teraterm::ohos::MacroStepResult macro_result = macro_iter->second.runner->Advance(session);
  if (!IsMacroActive(macro_result)) {
    g_macros.erase(macro_iter);
  }
  return MakeMacroResult(env, macro_id, macro_result);
}

napi_value RespondMacroDialog(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value args[3];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 3) {
    return MakeError(env, "respondMacroDialog requires macro id, accepted flag, and value");
  }

  const uint32_t macro_id = ReadUint32(env, args[0], 0);
  const bool accepted = ReadBool(env, args[1], false);
  const std::string value = ReadString(env, args[2]);

  std::lock_guard<std::mutex> session_lock(g_sessions_mutex);
  std::lock_guard<std::mutex> macro_lock(g_macros_mutex);
  auto macro_iter = g_macros.find(macro_id);
  if (macro_iter == g_macros.end()) {
    return MakeError(env, "unknown macro id");
  }
  TerminalSession* session = FindSession(macro_iter->second.session_id);
  if (session == nullptr) {
    return MakeError(env, "macro session is missing");
  }
  if (!macro_iter->second.runner->RespondDialog(accepted, value)) {
    return MakeError(env, "macro is not waiting for a dialog response");
  }

  const teraterm::ohos::MacroStepResult macro_result = macro_iter->second.runner->Advance(session);
  if (!IsMacroActive(macro_result)) {
    g_macros.erase(macro_iter);
  }
  return MakeMacroResult(env, macro_id, macro_result);
}

napi_value StopMacro(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 1) {
    return MakeError(env, "stopMacro requires macro id");
  }

  const uint32_t macro_id = ReadUint32(env, args[0], 0);
  std::lock_guard<std::mutex> lock(g_macros_mutex);
  g_macros.erase(macro_id);
  return MakeUndefined(env);
}

napi_value ResizeSession(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value args[3];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 3) {
    return MakeError(env, "resizeSession requires id, rows, and cols");
  }

  const uint32_t id = ReadUint32(env, args[0], 0);
  const uint32_t rows = ReadUint32(env, args[1], 24);
  const uint32_t cols = ReadUint32(env, args[2], 80);

  std::lock_guard<std::mutex> lock(g_sessions_mutex);
  TerminalSession* session = FindSession(id);
  if (session == nullptr) {
    return MakeError(env, "unknown session id");
  }
  session->Resize(rows, cols);
  return MakeUndefined(env);
}

napi_value ReadSnapshot(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 1) {
    return MakeError(env, "readSnapshot requires id");
  }

  const uint32_t id = ReadUint32(env, args[0], 0);

  std::lock_guard<std::mutex> lock(g_sessions_mutex);
  TerminalSession* session = FindSession(id);
  if (session == nullptr) {
    return MakeError(env, "unknown session id");
  }

  const auto snapshot = session->Snapshot();
  napi_value result;
  napi_create_object(env, &result);

  napi_value rows;
  napi_create_array_with_length(env, snapshot.rows.size(), &rows);
  for (uint32_t index = 0; index < snapshot.rows.size(); ++index) {
    napi_value row;
    napi_create_string_utf8(env, snapshot.rows[index].c_str(), snapshot.rows[index].size(), &row);
    napi_set_element(env, rows, index, row);
  }

  napi_value scrollback_rows;
  napi_create_array_with_length(env, snapshot.scrollback_rows.size(), &scrollback_rows);
  for (uint32_t index = 0; index < snapshot.scrollback_rows.size(); ++index) {
    napi_value row;
    napi_create_string_utf8(env, snapshot.scrollback_rows[index].c_str(),
                            snapshot.scrollback_rows[index].size(), &row);
    napi_set_element(env, scrollback_rows, index, row);
  }

  napi_value cursor_row;
  napi_create_uint32(env, static_cast<uint32_t>(snapshot.cursor_row), &cursor_row);
  napi_value cursor_col;
  napi_create_uint32(env, static_cast<uint32_t>(snapshot.cursor_col), &cursor_col);

  napi_set_named_property(env, result, "scrollbackRows", scrollback_rows);
  napi_set_named_property(env, result, "rows", rows);

  napi_value cell_rows;
  napi_create_array_with_length(env, snapshot.cells.size(), &cell_rows);
  for (uint32_t row_index = 0; row_index < snapshot.cells.size(); ++row_index) {
    napi_value cell_row;
    napi_create_array_with_length(env, snapshot.cells[row_index].size(), &cell_row);
    for (uint32_t col_index = 0; col_index < snapshot.cells[row_index].size(); ++col_index) {
      napi_set_element(env, cell_row, col_index, MakeCell(env, snapshot.cells[row_index][col_index]));
    }
    napi_set_element(env, cell_rows, row_index, cell_row);
  }

  napi_set_named_property(env, result, "cells", cell_rows);
  napi_set_named_property(env, result, "cursorRow", cursor_row);
  napi_set_named_property(env, result, "cursorCol", cursor_col);
  return result;
}

napi_value CloseSession(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 1) {
    return MakeError(env, "closeSession requires id");
  }

  const uint32_t id = ReadUint32(env, args[0], 0);
  std::lock_guard<std::mutex> lock(g_sessions_mutex);
  g_sessions.erase(id);
  std::lock_guard<std::mutex> macro_lock(g_macros_mutex);
  for (auto iter = g_macros.begin(); iter != g_macros.end();) {
    if (iter->second.session_id == id) {
      iter = g_macros.erase(iter);
    } else {
      ++iter;
    }
  }
  return MakeUndefined(env);
}

napi_value ImportIniProfiles(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 1) {
    return MakeError(env, "importIniProfiles requires ini text");
  }

  const std::string ini_text = ReadString(env, args[0]);
  const auto profiles = teraterm::ohos::TeraTermIniImporter::ImportHostProfiles(ini_text);
  napi_value result;
  napi_create_array_with_length(env, profiles.size(), &result);
  for (uint32_t index = 0; index < profiles.size(); ++index) {
    napi_set_element(env, result, index, MakeProfile(env, profiles[index]));
  }
  return result;
}

napi_value ParseKnownHosts(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 1) {
    return MakeError(env, "parseKnownHosts requires known_hosts text");
  }

  const std::string known_hosts_text = ReadString(env, args[0]);
  const auto entries = teraterm::ohos::KnownHostsStore::Parse(known_hosts_text);

  napi_value result;
  napi_create_array_with_length(env, entries.size(), &result);
  for (uint32_t index = 0; index < entries.size(); ++index) {
    napi_set_element(env, result, index, MakeKnownHostEntry(env, entries[index]));
  }
  return result;
}

napi_value CheckKnownHost(napi_env env, napi_callback_info info) {
  size_t argc = 5;
  napi_value args[5];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 5) {
    return MakeError(env, "checkKnownHost requires known_hosts text, host, port, key type, and key");
  }

  const std::string known_hosts_text = ReadString(env, args[0]);
  const std::string host = ReadString(env, args[1]);
  const uint32_t port = ReadUint32(env, args[2], 22);
  const std::string key_type = ReadString(env, args[3]);
  const std::string key_base64 = ReadString(env, args[4]);

  const auto entries = teraterm::ohos::KnownHostsStore::Parse(known_hosts_text);
  const auto match = teraterm::ohos::KnownHostsStore::Match(
      entries, host, static_cast<uint16_t>(port), key_type, key_base64);
  return MakeKnownHostMatch(env, match);
}

napi_value UpdateKnownHosts(napi_env env, napi_callback_info info) {
  size_t argc = 6;
  napi_value args[6];
  napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  if (argc < 5) {
    return MakeError(env, "updateKnownHosts requires known_hosts text, host, port, key type, and key");
  }

  const std::string known_hosts_text = ReadString(env, args[0]);
  const std::string host = ReadString(env, args[1]);
  const uint32_t port = ReadUint32(env, args[2], 22);
  const std::string key_type = ReadString(env, args[3]);
  const std::string key_base64 = ReadString(env, args[4]);
  const std::string comment = argc > 5 ? ReadString(env, args[5]) : "";

  const std::string updated = teraterm::ohos::KnownHostsStore::AddOrUpdate(
      known_hosts_text, host, static_cast<uint16_t>(port), key_type, key_base64, comment);
  napi_value result;
  napi_create_string_utf8(env, updated.c_str(), updated.size(), &result);
  return result;
}

napi_value Init(napi_env env, napi_value exports) {
  napi_property_descriptor descriptors[] = {
      {"createSession", nullptr, CreateSession, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"writeSession", nullptr, WriteSession, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"connectTcpSession", nullptr, ConnectTcpSession, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"connectTcpSessionWithProxy", nullptr, ConnectTcpSessionWithProxy, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"connectTelnetSession", nullptr, ConnectTelnetSession, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"connectTelnetSessionWithProxy", nullptr, ConnectTelnetSessionWithProxy, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"connectSerialSession", nullptr, ConnectSerialSession, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"listSerialDevices", nullptr, ListSerialDevices, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"connectSshSession", nullptr, ConnectSshSession, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"scpDownload", nullptr, ScpDownload, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"scpUpload", nullptr, ScpUpload, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"sftpDownload", nullptr, SftpDownload, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"sftpUpload", nullptr, SftpUpload, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"startSshLocalForward", nullptr, StartSshLocalForward, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"stopSshLocalForward", nullptr, StopSshLocalForward, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"sendInput", nullptr, SendInput, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"pumpSession", nullptr, PumpSession, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"disconnectSession", nullptr, DisconnectSession, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"drainTranscript", nullptr, DrainTranscript, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"readOutputHistory", nullptr, ReadOutputHistory, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"startMacro", nullptr, StartMacro, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"pumpMacro", nullptr, PumpMacro, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"respondMacroDialog", nullptr, RespondMacroDialog, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"stopMacro", nullptr, StopMacro, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"resizeSession", nullptr, ResizeSession, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"readSnapshot", nullptr, ReadSnapshot, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"closeSession", nullptr, CloseSession, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"importIniProfiles", nullptr, ImportIniProfiles, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"parseKnownHosts", nullptr, ParseKnownHosts, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"checkKnownHost", nullptr, CheckKnownHost, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"updateKnownHosts", nullptr, UpdateKnownHosts, nullptr, nullptr, nullptr, napi_default, nullptr},
  };
  napi_define_properties(env, exports, sizeof(descriptors) / sizeof(descriptors[0]), descriptors);
  return exports;
}

}  // namespace

NAPI_MODULE(teraterm_ohos, Init)
