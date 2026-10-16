#include "telnet_filter.h"
#include "terminal_session.h"

#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using teraterm::ohos::TelnetFilter;
using teraterm::ohos::TerminalSession;
using teraterm::ohos::ProxyOpenOptions;
using teraterm::ohos::ProxyType;
using teraterm::ohos::SerialFlowControl;
using teraterm::ohos::SerialOpenOptions;
using teraterm::ohos::SerialParity;

namespace {

constexpr uint8_t kWill = 251;
constexpr uint8_t kSe = 240;
constexpr uint8_t kSb = 250;
constexpr uint8_t kDo = 253;
constexpr uint8_t kIac = 255;
constexpr uint8_t kEcho = 1;
constexpr uint8_t kSuppressGoAhead = 3;
constexpr uint8_t kNaws = 31;

int MakeServer(uint16_t* port) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);

  int reuse = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  assert(bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
  assert(listen(fd, 1) == 0);

  socklen_t len = sizeof(addr);
  assert(getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
  *port = ntohs(addr.sin_port);
  return fd;
}

bool SnapshotContains(const TerminalSession& session, const std::string& text) {
  const auto snapshot = session.Snapshot();
  for (const auto& row : snapshot.rows) {
    if (row.find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
}

void PumpUntil(TerminalSession* session, const std::string& text) {
  for (int i = 0; i < 50; ++i) {
    session->Pump(4096);
    if (SnapshotContains(*session, text)) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  std::cerr << "missing terminal text: " << text << "\n";
  std::cerr << "history: " << session->OutputHistory() << "\n";
  assert(false && "expected terminal text was not received");
}

std::string RecvUntilHeaders(int fd) {
  std::string text;
  char buffer[128] = {};
  while (text.find("\r\n\r\n") == std::string::npos) {
    const ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
    assert(received > 0);
    text.append(buffer, static_cast<std::size_t>(received));
  }
  return text;
}

std::string RecvLine(int fd) {
  std::string text;
  char ch = '\0';
  while (text.find('\n') == std::string::npos) {
    const ssize_t received = recv(fd, &ch, 1, 0);
    assert(received > 0);
    text.push_back(ch);
  }
  return text;
}

std::vector<uint8_t> RecvExact(int fd, std::size_t size) {
  std::vector<uint8_t> data(size);
  std::size_t offset = 0;
  while (offset < size) {
    const ssize_t received = recv(fd, data.data() + offset, size - offset, 0);
    assert(received > 0);
    offset += static_cast<std::size_t>(received);
  }
  return data;
}

std::string ReadFdOnce(int fd) {
  fd_set read_set;
  FD_ZERO(&read_set);
  FD_SET(fd, &read_set);
  timeval timeout = {};
  timeout.tv_sec = 2;
  const int ready = select(fd + 1, &read_set, nullptr, nullptr, &timeout);
  assert(ready > 0);
  char buffer[128] = {};
  const ssize_t received = read(fd, buffer, sizeof(buffer));
  assert(received > 0);
  return std::string(buffer, static_cast<std::size_t>(received));
}

int MakePseudoTerminal(std::string* slave_path) {
  const int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
  assert(master_fd >= 0);
  assert(grantpt(master_fd) == 0);
  assert(unlockpt(master_fd) == 0);
  char* path = ptsname(master_fd);
  assert(path != nullptr);
  *slave_path = path;
  return master_fd;
}

}  // namespace

int main() {
  {
    TelnetFilter filter;
    std::vector<uint8_t> input = {'H', 'i', kIac, kWill, kEcho, kIac, kDo, kSuppressGoAhead,
                                  kIac, kIac, '!'};
    std::vector<uint8_t> terminal_output;
    std::vector<uint8_t> reply;
    filter.ProcessInbound(input, &terminal_output, &reply);

    const std::vector<uint8_t> expected_output = {'H', 'i', kIac, '!'};
    const std::vector<uint8_t> expected_reply = {kIac, kDo, kEcho, kIac, kWill, kSuppressGoAhead};
    assert(terminal_output == expected_output);
    assert(reply == expected_reply);
  }

  uint16_t port = 0;
  const int server_fd = MakeServer(&port);
  std::thread server([server_fd]() {
    const int client = accept(server_fd, nullptr, nullptr);
    assert(client >= 0);
    const char* greeting = "server ready\r\n";
    send(client, greeting, std::strlen(greeting), 0);

    char buffer[64] = {};
    const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
    assert(received > 0);
    const std::string text(buffer, static_cast<std::size_t>(received));
    assert(text.find("ping") != std::string::npos);

    const char* response = "pong\r\n";
    send(client, response, std::strlen(response), 0);
    close(client);
    close(server_fd);
  });

  TerminalSession session(5, 40);
  std::string error;
  assert(session.ConnectTcp("127.0.0.1", port, 2000, &error));
  PumpUntil(&session, "server ready");
  assert(session.SendInput("ping\n", &error));
  PumpUntil(&session, "pong");
  const std::string transcript = session.DrainTranscript();
  assert(transcript.find("server ready") != std::string::npos);
  assert(transcript.find("pong") != std::string::npos);
  assert(session.DrainTranscript().empty());

  session.Disconnect();
  server.join();

  uint16_t proxy_port = 0;
  const int proxy_fd = MakeServer(&proxy_port);
  std::thread proxy_server([proxy_fd]() {
    const int client = accept(proxy_fd, nullptr, nullptr);
    assert(client >= 0);
    const std::string request = RecvUntilHeaders(client);
    assert(request.find("CONNECT target.example.com:2222 HTTP/1.1") != std::string::npos);
    assert(request.find("Proxy-Authorization: Basic dXNlcjpwYXNz") != std::string::npos);

    const char* connected = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send(client, connected, std::strlen(connected), 0);
    const char* greeting = "proxy ready\r\n";
    send(client, greeting, std::strlen(greeting), 0);

    char buffer[64] = {};
    const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
    assert(received > 0);
    const std::string text(buffer, static_cast<std::size_t>(received));
    assert(text.find("proxy ping") != std::string::npos);

    const char* response = "proxy pong\r\n";
    send(client, response, std::strlen(response), 0);
    close(client);
    close(proxy_fd);
  });

  TerminalSession proxy_session(5, 40);
  ProxyOpenOptions proxy;
  proxy.type = ProxyType::HttpConnect;
  proxy.host = "127.0.0.1";
  proxy.port = proxy_port;
  proxy.username = "user";
  proxy.password = "pass";
  assert(proxy_session.ConnectTcp("target.example.com", 2222, 2000, proxy, &error));
  PumpUntil(&proxy_session, "proxy ready");
  assert(proxy_session.SendInput("proxy ping\n", &error));
  PumpUntil(&proxy_session, "proxy pong");
  proxy_session.Disconnect();
  proxy_server.join();

  uint16_t socks_port = 0;
  const int socks_fd = MakeServer(&socks_port);
  std::thread socks_server([socks_fd]() {
    const int client = accept(socks_fd, nullptr, nullptr);
    assert(client >= 0);

    const std::vector<uint8_t> greeting = RecvExact(client, 4);
    assert(greeting == std::vector<uint8_t>({0x05, 0x02, 0x00, 0x02}));
    const std::vector<uint8_t> method = {0x05, 0x02};
    send(client, method.data(), method.size(), 0);

    std::vector<uint8_t> auth = RecvExact(client, 2);
    assert(auth[0] == 0x01);
    const std::vector<uint8_t> username = RecvExact(client, auth[1]);
    const std::vector<uint8_t> password_length = RecvExact(client, 1);
    const std::vector<uint8_t> password = RecvExact(client, password_length[0]);
    assert(std::string(username.begin(), username.end()) == "user");
    assert(std::string(password.begin(), password.end()) == "pass");
    const std::vector<uint8_t> auth_ok = {0x01, 0x00};
    send(client, auth_ok.data(), auth_ok.size(), 0);

    const std::vector<uint8_t> request_prefix = RecvExact(client, 5);
    assert(request_prefix[0] == 0x05);
    assert(request_prefix[1] == 0x01);
    assert(request_prefix[2] == 0x00);
    assert(request_prefix[3] == 0x03);
    const std::vector<uint8_t> target_host = RecvExact(client, request_prefix[4]);
    const std::vector<uint8_t> target_port = RecvExact(client, 2);
    assert(std::string(target_host.begin(), target_host.end()) == "socks-target.example.com");
    assert(target_port[0] == 0x08 && target_port[1] == 0xae);

    const std::vector<uint8_t> connected = {0x05, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    send(client, connected.data(), connected.size(), 0);
    const char* greeting_text = "socks ready\r\n";
    send(client, greeting_text, std::strlen(greeting_text), 0);

    char buffer[64] = {};
    const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
    assert(received > 0);
    const std::string text(buffer, static_cast<std::size_t>(received));
    assert(text.find("socks ping") != std::string::npos);

    const char* response = "socks pong\r\n";
    send(client, response, std::strlen(response), 0);
    close(client);
    close(socks_fd);
  });

  TerminalSession socks_session(5, 40);
  ProxyOpenOptions socks;
  socks.type = ProxyType::Socks5;
  socks.host = "127.0.0.1";
  socks.port = socks_port;
  socks.username = "user";
  socks.password = "pass";
  assert(socks_session.ConnectTcp("socks-target.example.com", 2222, 2000, socks, &error));
  PumpUntil(&socks_session, "socks ready");
  assert(socks_session.SendInput("socks ping\n", &error));
  PumpUntil(&socks_session, "socks pong");
  socks_session.Disconnect();
  socks_server.join();

  uint16_t socks4_port = 0;
  const int socks4_fd = MakeServer(&socks4_port);
  std::thread socks4_server([socks4_fd]() {
    const int client = accept(socks4_fd, nullptr, nullptr);
    assert(client >= 0);

    const std::vector<uint8_t> request_prefix = RecvExact(client, 8);
    assert(request_prefix[0] == 0x04);
    assert(request_prefix[1] == 0x01);
    assert(request_prefix[2] == 0x08 && request_prefix[3] == 0xae);
    assert(request_prefix[4] == 0x00 && request_prefix[5] == 0x00);
    assert(request_prefix[6] == 0x00 && request_prefix[7] == 0x01);

    std::string user_id;
    char ch = '\0';
    while (recv(client, &ch, 1, 0) == 1 && ch != '\0') {
      user_id.push_back(ch);
    }
    assert(user_id == "legacy-user");

    std::string target_host;
    while (recv(client, &ch, 1, 0) == 1 && ch != '\0') {
      target_host.push_back(ch);
    }
    assert(target_host == "socks4-target.example.com");

    const std::vector<uint8_t> connected = {0x00, 0x5a, 0x08, 0xae, 0x00, 0x00, 0x00, 0x01};
    send(client, connected.data(), connected.size(), 0);
    const char* greeting_text = "socks4 ready\r\n";
    send(client, greeting_text, std::strlen(greeting_text), 0);

    char buffer[64] = {};
    const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
    assert(received > 0);
    const std::string text(buffer, static_cast<std::size_t>(received));
    assert(text.find("socks4 ping") != std::string::npos);

    const char* response = "socks4 pong\r\n";
    send(client, response, std::strlen(response), 0);
    close(client);
    close(socks4_fd);
  });

  TerminalSession socks4_session(5, 40);
  ProxyOpenOptions socks4;
  socks4.type = ProxyType::Socks4;
  socks4.host = "127.0.0.1";
  socks4.port = socks4_port;
  socks4.username = "legacy-user";
  assert(socks4_session.ConnectTcp("socks4-target.example.com", 2222, 2000, socks4, &error));
  PumpUntil(&socks4_session, "socks4 ready");
  assert(socks4_session.SendInput("socks4 ping\n", &error));
  PumpUntil(&socks4_session, "socks4 pong");
  socks4_session.Disconnect();
  socks4_server.join();

  uint16_t telnet_proxy_port = 0;
  const int telnet_proxy_fd = MakeServer(&telnet_proxy_port);
  std::thread telnet_proxy_server([telnet_proxy_fd]() {
    const int client = accept(telnet_proxy_fd, nullptr, nullptr);
    assert(client >= 0);

    const char* host_prompt = ">> Host name: ";
    send(client, host_prompt, std::strlen(host_prompt), 0);
    const std::string target = RecvLine(client);
    assert(target.find("telnet-target.example.com:2323") != std::string::npos);

    const char* user_prompt = "Username:";
    send(client, user_prompt, std::strlen(user_prompt), 0);
    const std::string user = RecvLine(client);
    assert(user.find("telnet-user") != std::string::npos);

    const char* pass_prompt = "Password:";
    send(client, pass_prompt, std::strlen(pass_prompt), 0);
    const std::string password = RecvLine(client);
    assert(password.find("telnet-pass") != std::string::npos);

    const char* connected = "-- Connected to telnet-target.example.com\r\n";
    send(client, connected, std::strlen(connected), 0);
    const char* greeting_text = "telnet proxy ready\r\n";
    send(client, greeting_text, std::strlen(greeting_text), 0);

    char buffer[64] = {};
    const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
    assert(received > 0);
    const std::string text(buffer, static_cast<std::size_t>(received));
    assert(text.find("telnet proxy ping") != std::string::npos);

    const char* response = "telnet proxy pong\r\n";
    send(client, response, std::strlen(response), 0);
    close(client);
    close(telnet_proxy_fd);
  });

  TerminalSession telnet_proxy_session(5, 48);
  ProxyOpenOptions telnet_proxy;
  telnet_proxy.type = ProxyType::Telnet;
  telnet_proxy.host = "127.0.0.1";
  telnet_proxy.port = telnet_proxy_port;
  telnet_proxy.username = "telnet-user";
  telnet_proxy.password = "telnet-pass";
  assert(telnet_proxy_session.ConnectTcp("telnet-target.example.com", 2323, 2000, telnet_proxy, &error));
  PumpUntil(&telnet_proxy_session, "telnet proxy ready");
  assert(telnet_proxy_session.SendInput("telnet proxy ping\n", &error));
  PumpUntil(&telnet_proxy_session, "telnet proxy pong");
  telnet_proxy_session.Disconnect();
  telnet_proxy_server.join();

  uint16_t telnet_resize_port = 0;
  const int telnet_resize_fd = MakeServer(&telnet_resize_port);
  std::thread telnet_resize_server([telnet_resize_fd]() {
    const int client = accept(telnet_resize_fd, nullptr, nullptr);
    assert(client >= 0);

    const std::vector<uint8_t> expected = {
        kIac, kSb, kNaws, 0x00, 0x84, 0x00, 0x0b, kIac, kSe,
    };
    const std::vector<uint8_t> resize_command = RecvExact(client, expected.size());
    assert(resize_command == expected);

    const char* greeting_text = "telnet resized\r\n";
    send(client, greeting_text, std::strlen(greeting_text), 0);
    close(client);
    close(telnet_resize_fd);
  });

  TerminalSession telnet_resize_session(5, 40);
  assert(telnet_resize_session.ConnectTelnet("127.0.0.1", telnet_resize_port, 2000, &error));
  telnet_resize_session.Resize(11, 132);
  PumpUntil(&telnet_resize_session, "telnet resized");
  telnet_resize_session.Disconnect();
  telnet_resize_server.join();

  std::string slave_path;
  const int master_fd = MakePseudoTerminal(&slave_path);
  TerminalSession serial_session(5, 40);
  SerialOpenOptions serial_options;
  serial_options.device_path = slave_path;
  serial_options.baud_rate = 9600;
  serial_options.data_bits = 8;
  serial_options.stop_bits = 1;
  serial_options.parity = SerialParity::None;
  serial_options.flow_control = SerialFlowControl::None;
  assert(serial_session.ConnectSerial(serial_options, &error));

  const char* serial_greeting = "serial ready\r\n";
  write(master_fd, serial_greeting, std::strlen(serial_greeting));

  PumpUntil(&serial_session, "serial ready");
  assert(serial_session.SendInput("serial ping\n", &error));
  assert(ReadFdOnce(master_fd).find("serial ping") != std::string::npos);
  const char* serial_response = "serial pong\r\n";
  write(master_fd, serial_response, std::strlen(serial_response));
  PumpUntil(&serial_session, "serial pong");
  serial_session.Disconnect();
  close(master_fd);
  return 0;
}
