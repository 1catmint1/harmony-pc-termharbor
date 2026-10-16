#include "macro_runner.h"
#include "terminal_session.h"

#include <cassert>
#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using teraterm::ohos::MacroRunner;
using teraterm::ohos::MacroStatus;
using teraterm::ohos::TerminalSession;

namespace {

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

}  // namespace

int main() {
  {
    TerminalSession session(5, 40);
    session.WriteLocalOutput("login: ");
    MacroRunner runner("wait 'login: '\nsendln 'guest'\nend\n", {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Error);
    assert(result.message.find("session is not connected") != std::string::npos);
  }

  {
    TerminalSession session(5, 40);
    MacroRunner runner("wait 'ready'\nsend 'ok'\n", {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Waiting);
    session.WriteLocalOutput("system ready");
    result = runner.Advance(&session);
    assert(result.status == MacroStatus::Error);
  }

  {
    TerminalSession session(5, 40);
    MacroRunner runner("mpause 20\nend\n", {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Waiting);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);
  }

  {
    TerminalSession session(5, 40);
    MacroRunner runner("unsupported\n", {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Error);
    assert(result.message.find("unsupported macro command") != std::string::npos);
  }

  {
    TerminalSession session(5, 40);
    MacroRunner runner("wait\n", {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Error);
    assert(result.message.find("requires at least one text argument") != std::string::npos);
  }

  {
    uint16_t port = 0;
    const int server_fd = MakeServer(&port);
    std::string received_text;
    std::thread server([server_fd, &received_text]() {
      const int client = accept(server_fd, nullptr, nullptr);
      assert(client >= 0);
      const char* greeting = "login: ";
      send(client, greeting, std::strlen(greeting), 0);

      char buffer[64] = {};
      const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
      assert(received > 0);
      received_text.assign(buffer, static_cast<std::size_t>(received));
      close(client);
      close(server_fd);
    });

    TerminalSession session(5, 40);
    const std::string script = "password = 'guest'\n"
                               "connect '127.0.0.1:" +
                               std::to_string(port) +
                               " /nossh'\n"
                               ":login\n"
                               "wait 'login: '\n"
                               "if result=0 goto login\n"
                               "sendln password\n"
                               "end\n";
    MacroRunner runner(script, {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Waiting);
    for (int i = 0; i < 50 && result.status == MacroStatus::Waiting; ++i) {
      session.Pump(4096);
      result = runner.Advance(&session);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    assert(result.status == MacroStatus::Completed);
    server.join();
    assert(received_text.find("guest") != std::string::npos);
  }

  {
    TerminalSession session(5, 40);
    MacroRunner runner("mtimeout = 20\nwait 'never arrives'\nif result=0 goto done\nsendln 'bad'\n:done\nend\n", {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Waiting);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);
  }

  {
    TerminalSession session(5, 40);
    session.WriteLocalOutput("user@host$ ");
    MacroRunner runner("waitregex 'user@.*[$] '\nif result=1 goto ok\nend\n:ok\nend\n", {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);
  }

  {
    const std::string source_path = "/tmp/teraterm_ohos_macro_source.txt";
    const std::string copy_path = "/tmp/teraterm_ohos_macro_copy.txt";
    std::remove(source_path.c_str());
    std::remove(copy_path.c_str());

    TerminalSession session(5, 40);
    const std::string script = "part = 'al'\n"
                               "strconcat part 'pha'\n"
                               "fileopen fh '" +
                               source_path +
                               "' 0\n"
                               "filewriteln fh part\n"
                               "fileclose fh\n"
                               "fileopen fh '" +
                               source_path +
                               "' 0 1\n"
                               "filereadln fh line\n"
                               "fileclose fh\n"
                               "fileopen out '" +
                               copy_path +
                               "' 0\n"
                               "filewrite out line\n"
                               "fileclose out\n"
                               "end\n";
    MacroRunner runner(script, {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);

    std::ifstream copy(copy_path, std::ios::binary);
    std::string content;
    std::getline(copy, content);
    assert(content == "alpha");
    std::remove(source_path.c_str());
    std::remove(copy_path.c_str());
  }

  {
    const std::string output_path = "/tmp/teraterm_ohos_macro_block_if.txt";
    std::remove(output_path.c_str());

    TerminalSession session(5, 40);
    const std::string script = "fileopen out '" +
                               output_path +
                               "' 0\n"
                               "flag = 1\n"
                               "if flag=1 then\n"
                               "filewriteln out 'true'\n"
                               "else\n"
                               "filewriteln out 'false'\n"
                               "endif\n"
                               "flag = 2\n"
                               "if flag=1 then\n"
                               "filewriteln out 'bad-if'\n"
                               "elseif flag=2 then\n"
                               "filewriteln out 'elseif'\n"
                               "else\n"
                               "filewriteln out 'bad-else'\n"
                               "endif\n"
                               "if flag=0 then\n"
                               "if flag=0 then\n"
                               "filewriteln out 'bad-nested'\n"
                               "endif\n"
                               "else\n"
                               "filewriteln out 'nested-else'\n"
                               "endif\n"
                               "fileclose out\n"
                               "end\n";
    MacroRunner runner(script, {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);

    std::ifstream output(output_path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(output)), std::istreambuf_iterator<char>());
    assert(content.find("true\n") != std::string::npos);
    assert(content.find("elseif\n") != std::string::npos);
    assert(content.find("nested-else\n") != std::string::npos);
    assert(content.find("bad") == std::string::npos);
    std::remove(output_path.c_str());
  }

  {
    const std::string output_path = "/tmp/teraterm_ohos_macro_while.txt";
    std::remove(output_path.c_str());

    TerminalSession session(5, 40);
    const std::string script = "fileopen out '" +
                               output_path +
                               "' 0\n"
                               "i = 0\n"
                               "while i < 3\n"
                               "filewriteln out i\n"
                               "i = i + 1\n"
                               "endwhile\n"
                               "while i < 0\n"
                               "while i < 3\n"
                               "filewriteln out 'bad-nested-loop'\n"
                               "endwhile\n"
                               "endwhile\n"
                               "fileclose out\n"
                               "end\n";
    MacroRunner runner(script, {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);

    std::ifstream output(output_path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(output)), std::istreambuf_iterator<char>());
    assert(content == "0\n1\n2\n");
    assert(content.find("bad") == std::string::npos);
    std::remove(output_path.c_str());
  }

  {
    const std::string output_path = "/tmp/teraterm_ohos_macro_call_return.txt";
    std::remove(output_path.c_str());

    TerminalSession session(5, 40);
    const std::string script = "fileopen out '" +
                               output_path +
                               "' 0\n"
                               "filewriteln out 'main-start'\n"
                               "call sub\n"
                               "filewriteln out 'main-after'\n"
                               "if 1=1 call inline\n"
                               "filewriteln out 'main-inline-after'\n"
                               "fileclose out\n"
                               "end\n"
                               ":sub\n"
                               "filewriteln out 'sub-start'\n"
                               "call nested\n"
                               "filewriteln out 'sub-after'\n"
                               "return\n"
                               ":nested\n"
                               "filewriteln out 'nested'\n"
                               "return\n"
                               ":inline\n"
                               "filewriteln out 'inline'\n"
                               "return\n";
    MacroRunner runner(script, {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);

    std::ifstream output(output_path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(output)), std::istreambuf_iterator<char>());
    assert(content == "main-start\nsub-start\nnested\nsub-after\nmain-after\ninline\nmain-inline-after\n");
    std::remove(output_path.c_str());
  }

  {
    TerminalSession session(5, 40);
    MacroRunner runner("return\n", {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Error);
    assert(result.message.find("return requires an active subroutine call") != std::string::npos);
  }

  {
    TerminalSession session(5, 40);
    MacroRunner runner("call sub\n:sub\n", {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Error);
    assert(result.message.find("subroutine reached end without return") != std::string::npos);
  }

  {
    const std::string output_path = "/tmp/teraterm_ohos_macro_loop_variants.txt";
    std::remove(output_path.c_str());

    TerminalSession session(5, 40);
    const std::string script = "fileopen out '" +
                               output_path +
                               "' 0\n"
                               "i = 0\n"
                               "do while i < 3\n"
                               "filewriteln out i\n"
                               "i = i + 1\n"
                               "loop\n"
                               "j = 0\n"
                               "do\n"
                               "j = j + 1\n"
                               "filewriteln out j\n"
                               "loop until j = 2\n"
                               "k = 0\n"
                               "do until k = 2\n"
                               "k = k + 1\n"
                               "filewriteln out k\n"
                               "loop\n"
                               "m = 0\n"
                               "until m = 2\n"
                               "m = m + 1\n"
                               "filewriteln out m\n"
                               "enduntil\n"
                               "do while 0\n"
                               "do\n"
                               "filewriteln out 'bad-nested-do'\n"
                               "loop\n"
                               "loop\n"
                               "until 1\n"
                               "filewriteln out 'bad-until'\n"
                               "enduntil\n"
                               "fileclose out\n"
                               "end\n";
    MacroRunner runner(script, {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);

    std::ifstream output(output_path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(output)), std::istreambuf_iterator<char>());
    assert(content == "0\n1\n2\n1\n2\n1\n2\n1\n2\n");
    assert(content.find("bad") == std::string::npos);
    std::remove(output_path.c_str());
  }

  {
    const std::string output_path = "/tmp/teraterm_ohos_macro_for_break_continue.txt";
    std::remove(output_path.c_str());

    TerminalSession session(5, 40);
    const std::string script = "fileopen out '" +
                               output_path +
                               "' 0\n"
                               "for i 1 3\n"
                               "filewriteln out i\n"
                               "next\n"
                               "for j 3 1\n"
                               "filewriteln out j\n"
                               "next\n"
                               "for k 1 5\n"
                               "if k = 2 continue\n"
                               "if k = 4 break\n"
                               "filewriteln out k\n"
                               "next\n"
                               "a = 0\n"
                               "while a < 5\n"
                               "a = a + 1\n"
                               "if a = 2 continue\n"
                               "if a = 4 break\n"
                               "filewriteln out a\n"
                               "endwhile\n"
                               "fileclose out\n"
                               "end\n";
    MacroRunner runner(script, {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);

    std::ifstream output(output_path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(output)), std::istreambuf_iterator<char>());
    assert(content == "1\n2\n3\n3\n2\n1\n1\n3\n1\n3\n");
    std::remove(output_path.c_str());
  }

  {
    const std::string output_path = "/tmp/teraterm_ohos_macro_arrays.txt";
    std::remove(output_path.c_str());

    TerminalSession session(5, 40);
    const std::string script = "fileopen out '" +
                               output_path +
                               "' 0\n"
                               "intdim fibonacci 6\n"
                               "fibonacci[0] = 0\n"
                               "fibonacci[1] = 1\n"
                               "for i 2 5\n"
                               "fibonacci[i] = fibonacci[i-2] + fibonacci[i-1]\n"
                               "next\n"
                               "for i 0 5\n"
                               "filewriteln out fibonacci[i]\n"
                               "next\n"
                               "strdim words 3\n"
                               "words[0] = 'alpha'\n"
                               "words[1] = 'beta'\n"
                               "words[2] = words[0]\n"
                               "strconcat words[2] '-tail'\n"
                               "filewriteln out words[2]\n"
                               "x = (2 + 3) * 4 / 2\n"
                               "filewriteln out x\n"
                               "fileclose out\n"
                               "end\n";
    MacroRunner runner(script, {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);

    std::ifstream output(output_path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(output)), std::istreambuf_iterator<char>());
    assert(content == "0\n1\n1\n2\n3\n5\nalpha-tail\n10\n");
    std::remove(output_path.c_str());
  }

  {
    const std::string output_path = "/tmp/teraterm_ohos_macro_string_commands.txt";
    std::remove(output_path.c_str());

    TerminalSession session(5, 40);
    const std::string script = "fileopen out '" +
                               output_path +
                               "' 0\n"
                               "strcompare 'abc' 'def'\n"
                               "filewriteln out result\n"
                               "strcompare 'same' 'same'\n"
                               "filewriteln out result\n"
                               "strcompare 'z' 'a'\n"
                               "filewriteln out result\n"
                               "strscan 'tera term' 'term'\n"
                               "filewriteln out result\n"
                               "strscan 'tera term' 'none'\n"
                               "filewriteln out result\n"
                               "strcopy '0123456789ABCDEF' (14%5)+1 3 part\n"
                               "filewriteln out part\n"
                               "strdim parts 2\n"
                               "strcopy 'alpha-beta' 7 4 parts[1]\n"
                               "filewriteln out parts[1]\n"
                               "bits = 12 & 10\n"
                               "filewriteln out bits\n"
                               "fileclose out\n"
                               "end\n";
    MacroRunner runner(script, {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);

    std::ifstream output(output_path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(output)), std::istreambuf_iterator<char>());
    assert(content == "-1\n0\n1\n6\n0\n456\nbeta\n8\n");
    std::remove(output_path.c_str());
  }

  {
    const std::string output_path = "/tmp/teraterm_ohos_macro_sprintf.txt";
    std::remove(output_path.c_str());

    TerminalSession session(5, 40);
    const std::string script = "fileopen out '" +
                               output_path +
                               "' 0\n"
                               "sprintf 'Tera Term 4.%d' 51\n"
                               "filewriteln out inputstr\n"
                               "sprintf2 line '%s=%d %s=0x%02X %%' 'dec' 10 'hex' 33\n"
                               "filewriteln out line\n"
                               "strdim lines 2\n"
                               "sprintf2 lines[0] '%-6s:%04d:%#x:%c' 'id' 7 33 65\n"
                               "filewriteln out lines[0]\n"
                               "sprintf2 lines[1] 'pi=%.2f sci=%.1E' '3.14159' '3.14159'\n"
                               "filewriteln out lines[1]\n"
                               "fileclose out\n"
                               "end\n";
    MacroRunner runner(script, {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);

    std::ifstream output(output_path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(output)), std::istreambuf_iterator<char>());
    assert(content == "Tera Term 4.51\ndec=10 hex=0x21 %\nid    :0007:0x21:A\npi=3.14 sci=3.1E+00\n");
    std::remove(output_path.c_str());
  }

  {
    const std::string output_path = "/tmp/teraterm_ohos_macro_regex_strings.txt";
    std::remove(output_path.c_str());

    TerminalSession session(5, 40);
    const std::string script = "fileopen out '" +
                               output_path +
                               "' 0\n"
                               "strmatch 'Microsoft Windows XP [Version 5.1.2600]' '\\[(Version (\\d+).(\\d+).(\\d+))\\]'\n"
                               "filewriteln out result\n"
                               "filewriteln out matchstr\n"
                               "filewriteln out groupmatchstr1\n"
                               "filewriteln out groupmatchstr2\n"
                               "filewriteln out groupmatchstr3\n"
                               "filewriteln out groupmatchstr4\n"
                               "strmatch 'abc' 'z+'\n"
                               "filewriteln out result\n"
                               "src = 'linux.txt'\n"
                               "strreplace src 1 '\\.txt' '.doc'\n"
                               "filewriteln out result\n"
                               "filewriteln out src\n"
                               "filewriteln out matchstr\n"
                               "src = 'Microsoft Windows XP [Version 5.1.2600]'\n"
                               "strmatch src '(Version \\d+.\\d+.)\\d+'\n"
                               "pos = result\n"
                               "prefix = groupmatchstr1\n"
                               "strreplace src pos prefix 'Build '\n"
                               "filewriteln out result\n"
                               "filewriteln out src\n"
                               "filewriteln out matchstr\n"
                               "strreplace src 999 'no' 'x'\n"
                               "filewriteln out result\n"
                               "strreplace src 1 '[' 'x'\n"
                               "filewriteln out result\n"
                               "fileclose out\n"
                               "end\n";
    MacroRunner runner(script, {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);

    std::ifstream output(output_path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(output)), std::istreambuf_iterator<char>());
    assert(content == "22\n[Version 5.1.2600]\nVersion 5.1.2600\n5\n1\n2600\n0\n1\nlinux.doc\n.txt\n1\nMicrosoft Windows XP [Build 2600]\nVersion 5.1.\n0\n-1\n");
    std::remove(output_path.c_str());
  }

  {
    const std::string base_dir = "/tmp/teraterm_ohos_macro_file_ops";
    const std::string source_path = base_dir + "/source.txt";
    const std::string copy_path = base_dir + "/copy.txt";
    const std::string renamed_path = base_dir + "/renamed.txt";
    const std::string output_path = "/tmp/teraterm_ohos_macro_file_ops.txt";
    std::filesystem::remove_all(base_dir);
    std::remove(output_path.c_str());

    TerminalSession session(5, 40);
    const std::string script = "fileopen out '" +
                               output_path +
                               "' 0\n"
                               "foldercreate '" +
                               base_dir +
                               "'\n"
                               "filewriteln out result\n"
                               "foldersearch '" +
                               base_dir +
                               "'\n"
                               "filewriteln out result\n"
                               "filecreate fh '" +
                               source_path +
                               "'\n"
                               "filewriteln out result\n"
                               "filewriteln fh 'alpha'\n"
                               "filemarkptr fh\n"
                               "filewriteln fh 'beta'\n"
                               "fileseekback fh\n"
                               "filewrite fh 'B'\n"
                               "fileseek fh 0 0\n"
                               "filestrseek fh 'alph'\n"
                               "filewriteln out result\n"
                               "fileread fh 1 after\n"
                               "filewriteln out after\n"
                               "fileseek fh 0 2\n"
                               "filestrseek2 fh 'eta'\n"
                               "filewriteln out result\n"
                               "fileread fh 4 before\n"
                               "filewriteln out before\n"
                               "fileclose fh\n"
                               "filecopy '" +
                               source_path +
                               "' '" +
                               copy_path +
                               "'\n"
                               "filewriteln out result\n"
                               "filesearch '" +
                               copy_path +
                               "'\n"
                               "filewriteln out result\n"
                               "filestat '" +
                               copy_path +
                               "' size mtime drive\n"
                               "filewriteln out result\n"
                               "filewriteln out size\n"
                               "strlen mtime_len mtime\n"
                               "filewriteln out mtime_len\n"
                               "filewriteln out drive\n"
                               "filetruncate '" +
                               copy_path +
                               "' 5\n"
                               "filewriteln out result\n"
                               "filestat '" +
                               copy_path +
                               "' size2\n"
                               "filewriteln out size2\n"
                               "fileconcat '" +
                               copy_path +
                               "' '" +
                               source_path +
                               "'\n"
                               "filewriteln out result\n"
                               "filestat '" +
                               copy_path +
                               "' size3\n"
                               "filewriteln out size3\n"
                               "filerename '" +
                               copy_path +
                               "' '" +
                               renamed_path +
                               "'\n"
                               "filewriteln out result\n"
                               "filesearch '" +
                               copy_path +
                               "'\n"
                               "filewriteln out result\n"
                               "filesearch '" +
                               renamed_path +
                               "'\n"
                               "filewriteln out result\n"
                               "filedelete '" +
                               source_path +
                               "'\n"
                               "filewriteln out result\n"
                               "filedelete '" +
                               renamed_path +
                               "'\n"
                               "filewriteln out result\n"
                               "folderdelete '" +
                               base_dir +
                               "'\n"
                               "filewriteln out result\n"
                               "foldersearch '" +
                               base_dir +
                               "'\n"
                               "filewriteln out result\n"
                               "fileclose out\n"
                               "end\n";
    MacroRunner runner(script, {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);

    std::ifstream output(output_path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(output)), std::istreambuf_iterator<char>());
    assert(content == "0\n1\n0\n1\na\n1\nBeta\n0\n1\n0\n11\n19\n/\n0\n5\n0\n16\n0\n0\n1\n0\n0\n0\n0\n");
    assert(!std::filesystem::exists(base_dir));
    std::remove(output_path.c_str());
  }

  {
    const std::string base_dir = "/tmp/teraterm_ohos_macro_find_ops";
    const std::string output_path = "/tmp/teraterm_ohos_macro_find_ops.txt";
    std::filesystem::remove_all(base_dir);
    std::remove(output_path.c_str());

    TerminalSession session(5, 40);
    const std::string script = "fileopen out '" +
                               output_path +
                               "' 0\n"
                               "foldercreate '" +
                               base_dir +
                               "'\n"
                               "filecreate fh '" +
                               base_dir +
                               "/a.txt'\n"
                               "fileclose fh\n"
                               "filecreate fh '" +
                               base_dir +
                               "/B.TXT'\n"
                               "fileclose fh\n"
                               "filecreate fh '" +
                               base_dir +
                               "/c.log'\n"
                               "fileclose fh\n"
                               "findfirst dh '" +
                               base_dir +
                               "/*.txt' name\n"
                               "filewriteln out result\n"
                               "filewriteln out name\n"
                               "findnext dh name\n"
                               "filewriteln out result\n"
                               "filewriteln out name\n"
                               "findnext dh name\n"
                               "filewriteln out result\n"
                               "filewriteln out name\n"
                               "findclose dh\n"
                               "findfirst dh '" +
                               base_dir +
                               "/*.none' name\n"
                               "filewriteln out result\n"
                               "filewriteln out dh\n"
                               "filewriteln out name\n"
                               "findclose dh\n"
                               "filedelete '" +
                               base_dir +
                               "/a.txt'\n"
                               "filedelete '" +
                               base_dir +
                               "/B.TXT'\n"
                               "filedelete '" +
                               base_dir +
                               "/c.log'\n"
                               "folderdelete '" +
                               base_dir +
                               "'\n"
                               "fileclose out\n"
                               "end\n";
    MacroRunner runner(script, {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);

    std::ifstream output(output_path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(output)), std::istreambuf_iterator<char>());
    assert(content == "1\na.txt\n1\nB.TXT\n0\n\n0\n-1\n\n");
    assert(!std::filesystem::exists(base_dir));
    std::remove(output_path.c_str());
  }

  {
    const std::string base_dir = "/tmp/teraterm_ohos_macro_env_time_ops";
    const std::string output_path = "/tmp/teraterm_ohos_macro_env_time_ops.txt";
    std::filesystem::remove_all(base_dir);
    std::remove(output_path.c_str());
#if defined(_WIN32)
    _putenv_s("TT_OHOS_TEST_ENV", "portable");
#else
    setenv("TT_OHOS_TEST_ENV", "portable", 1);
#endif

    TerminalSession session(5, 40);
    const std::string script = "fileopen out '" +
                               output_path +
                               "' 0\n"
                               "foldercreate '" +
                               base_dir +
                               "'\n"
                               "setdir '" +
                               base_dir +
                               "'\n"
                               "filecreate fh 'relative.txt'\n"
                               "filewriteln fh 'rel'\n"
                               "fileclose fh\n"
                               "filesearch 'relative.txt'\n"
                               "filewriteln out result\n"
                               "getdir cwd\n"
                               "foldersearch cwd\n"
                               "filewriteln out result\n"
                               "getttdir ttdir\n"
                               "filewriteln out result\n"
                               "strlen ttlen ttdir\n"
                               "if ttlen > 0 then\n"
                               "filewriteln out 'ttdir-ok'\n"
                               "else\n"
                               "filewriteln out 'ttdir-bad'\n"
                               "endif\n"
                               "getenv 'TT_OHOS_TEST_ENV' env\n"
                               "filewriteln out env\n"
                               "expandenv expanded '%TT_OHOS_TEST_ENV%/tail/%TT_OHOS_MISSING_ENV%'\n"
                               "filewriteln out expanded\n"
                               "self = '%TT_OHOS_TEST_ENV%'\n"
                               "expandenv self\n"
                               "filewriteln out self\n"
                               "getdate date '%Y-%m-%d'\n"
                               "strlen date_len date\n"
                               "filewriteln out date_len\n"
                               "gettime time '%H:%M'\n"
                               "strlen time_len time\n"
                               "filewriteln out time_len\n"
                               "tz = 'GMT'\n"
                               "gettime utc '%Y' tz\n"
                               "strlen utc_len utc\n"
                               "filewriteln out utc_len\n"
                               "uptime up\n"
                               "if up >= 0 then\n"
                               "filewriteln out 'uptime-ok'\n"
                               "else\n"
                               "filewriteln out 'uptime-bad'\n"
                               "endif\n"
                               "filedelete 'relative.txt'\n"
                               "folderdelete '" +
                               base_dir +
                               "'\n"
                               "fileclose out\n"
                               "end\n";
    MacroRunner runner(script, {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);

    std::ifstream output(output_path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(output)), std::istreambuf_iterator<char>());
    assert(content == "1\n1\n1\nttdir-ok\nportable\nportable/tail/%TT_OHOS_MISSING_ENV%\nportable\n10\n5\n4\nuptime-ok\n");
    assert(!std::filesystem::exists(base_dir));
    std::remove(output_path.c_str());
#if defined(_WIN32)
    _putenv_s("TT_OHOS_TEST_ENV", "");
#else
    unsetenv("TT_OHOS_TEST_ENV");
#endif
  }

  {
    const std::string base_dir = "/tmp/teraterm_ohos_macro_log_ops";
    const std::string output_path = "/tmp/teraterm_ohos_macro_log_ops.txt";
    const std::string log_path = base_dir + "/session.log";
    std::filesystem::remove_all(base_dir);
    std::remove(output_path.c_str());

    TerminalSession session(5, 40);
    const std::string script = "fileopen out '" +
                               output_path +
                               "' 0\n"
                               "foldercreate '" +
                               base_dir +
                               "'\n"
                               "changedir '" +
                               base_dir +
                               "'\n"
                               "logopen 'session.log' 0 0 1 0 1\n"
                               "logwrite 'manual'#10\n"
                               "loginfo info\n"
                               "filewriteln out result\n"
                               "filesearch info\n"
                               "filewriteln out result\n"
                               "wait 'first'\n"
                               "logpause\n"
                               "wait 'hidden'\n"
                               "logstart\n"
                               "wait 'second'\n"
                               "logwrite 'tail'#10\n"
                               "logclose\n"
                               "loginfo after\n"
                               "filewriteln out result\n"
                               "fileclose out\n"
                               "end\n";
    MacroRunner runner(script, {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Waiting);

    session.WriteLocalOutput("prefix\x1b[31mfirst");
    result = runner.Advance(&session);
    assert(result.status == MacroStatus::Waiting);

    session.WriteLocalOutput("hidden");
    result = runner.Advance(&session);
    assert(result.status == MacroStatus::Waiting);

    session.WriteLocalOutput("second");
    result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);

    std::ifstream output(output_path, std::ios::binary);
    std::string output_content((std::istreambuf_iterator<char>(output)), std::istreambuf_iterator<char>());
    assert(output_content == "20\n1\n-1\n");

    std::ifstream log(log_path, std::ios::binary);
    std::string log_content((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
    assert(log_content.find("manual\n") != std::string::npos);
    assert(log_content.find("prefixfirst") != std::string::npos);
    assert(log_content.find("second") != std::string::npos);
    assert(log_content.find("tail\n") != std::string::npos);
    assert(log_content.find("hidden") == std::string::npos);
    assert(log_content.find("\x1b") == std::string::npos);

    std::remove(output_path.c_str());
    std::filesystem::remove_all(base_dir);
  }

  {
    const std::string output_path = "/tmp/teraterm_ohos_macro_dialogs.txt";
    std::remove(output_path.c_str());

    TerminalSession session(5, 40);
    const std::string script = "fileopen out '" +
                               output_path +
                               "' 0\n"
                               "messagebox 'Ready\\nNow' 'Notice' 1\n"
                               "inputbox 'User?' 'Input' 'guest'\n"
                               "filewriteln out inputstr\n"
                               "yesnobox 'Continue?' 'Question'\n"
                               "filewriteln out result\n"
                               "passwordbox 'Secret?' 'Password'\n"
                               "filewriteln out inputstr\n"
                               "fileclose out\n"
                               "end\n";
    MacroRunner runner(script, {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Waiting);
    assert(result.dialog_type == "messagebox");
    assert(result.dialog_title == "Notice");
    assert(result.dialog_text == "Ready\nNow");

    assert(runner.RespondDialog(true, ""));
    result = runner.Advance(&session);
    assert(result.status == MacroStatus::Waiting);
    assert(result.dialog_type == "inputbox");
    assert(result.dialog_default_text == "guest");

    assert(runner.RespondDialog(true, "alice"));
    result = runner.Advance(&session);
    assert(result.status == MacroStatus::Waiting);
    assert(result.dialog_type == "yesnobox");

    assert(runner.RespondDialog(false, ""));
    result = runner.Advance(&session);
    assert(result.status == MacroStatus::Waiting);
    assert(result.dialog_type == "passwordbox");
    assert(result.dialog_password);

    assert(runner.RespondDialog(true, "secret"));
    result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);

    std::ifstream output(output_path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(output)), std::istreambuf_iterator<char>());
    assert(content == "alice\n0\nsecret\n");
    std::remove(output_path.c_str());
  }

  {
    const std::string output_path = "/tmp/teraterm_ohos_macro_extended_dialogs.txt";
    std::remove(output_path.c_str());

    TerminalSession session(5, 40);
    const std::string script = "fileopen out '" +
                               output_path +
                               "' 0\n"
                               "strdim choices 3\n"
                               "choices[0] = 'alpha'\n"
                               "choices[1] = 'beta'\n"
                               "choices[2] = 'gamma'\n"
                               "statusbox 'Working\\nNow' 'Progress' 1\n"
                               "listbox 'Pick one' 'Choices' choices 2\n"
                               "filewriteln out result\n"
                               "filenamebox 'Save path' 1 '/tmp'\n"
                               "filewriteln out result\n"
                               "filewriteln out inputstr\n"
                               "dirnamebox 'Folder path' '/tmp'\n"
                               "filewriteln out result\n"
                               "filewriteln out inputstr\n"
                               "closesbox\n"
                               "fileclose out\n"
                               "end\n";
    MacroRunner runner(script, {});
    auto result = runner.Advance(&session);
    assert(result.status == MacroStatus::Waiting);
    assert(result.status_box_action == "show");
    assert(result.status_box_title == "Progress");
    assert(result.status_box_text == "Working\nNow");
    assert(result.dialog_type == "listbox");
    assert(result.dialog_items.size() == 3);
    assert(result.dialog_items[1] == "beta");
    assert(result.dialog_selected_index == 2);

    assert(runner.RespondDialog(true, "1"));
    result = runner.Advance(&session);
    assert(result.status == MacroStatus::Waiting);
    assert(result.dialog_type == "filenamebox");
    assert(result.dialog_save);
    assert(result.dialog_default_text == "/tmp");

    assert(runner.RespondDialog(true, "/tmp/out.txt"));
    result = runner.Advance(&session);
    assert(result.status == MacroStatus::Waiting);
    assert(result.dialog_type == "dirnamebox");
    assert(result.dialog_default_text == "/tmp");

    assert(runner.RespondDialog(true, "/tmp"));
    result = runner.Advance(&session);
    assert(result.status == MacroStatus::Completed);
    assert(result.status_box_action == "close");

    std::ifstream output(output_path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(output)), std::istreambuf_iterator<char>());
    assert(content == "1\n1\n/tmp/out.txt\n1\n/tmp\n");
    std::remove(output_path.c_str());
  }

  return 0;
}
