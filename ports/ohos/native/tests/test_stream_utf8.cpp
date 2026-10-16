#include "terminal_session_core.h"
#include <iostream>
#include <string>
using teraterm::ohos::TerminalSessionCore;
int main() {
  int checked = 0;
  const std::string samples[] = {"\xc2\xa3", "\xe4\xb8\xad", "\xf0\x9f\x8c\x8a", "A\xe4\xb8\xad\xc2\xa3\xf0\x9f\x8c\x8aZ"};
  for (const auto& sample : samples) {
    TerminalSessionCore whole(2, 40); whole.WriteUtf8(sample);
    for (std::size_t cut=1; cut<sample.size(); ++cut) {
      TerminalSessionCore stream(2, 40);
      stream.WriteUtf8(sample.substr(0,cut)); stream.WriteUtf8(sample.substr(cut));
      if (stream.Snapshot().rows != whole.Snapshot().rows || stream.Snapshot().cursor_col != whole.Snapshot().cursor_col) {
        std::cerr << "FAIL valid UTF-8 packet boundary cut=" << cut << " length=" << sample.size() << '\n'; return 1;
      }
      ++checked;
    }
    TerminalSessionCore bytes(2,40);
    for (char ch : sample) bytes.WriteUtf8(std::string(1,ch));
    if (bytes.Snapshot().rows != whole.Snapshot().rows || bytes.Snapshot().cursor_col != whole.Snapshot().cursor_col) return 2;
    ++checked;
  }
  TerminalSessionCore styled(2,40);
  const std::string color="\x1b[31m\xe4\xb8\xad\x1b[0mX";
  for (char ch:color) styled.WriteUtf8(std::string(1,ch));
  auto snapshot=styled.Snapshot();
  if(snapshot.rows[0]!="\xe4\xb8\xadX" || snapshot.cells[0][0].style.foreground!=1 || snapshot.cells[0][2].style.foreground!=7) return 3;
  ++checked;
  TerminalSessionCore cleared(2,40);cleared.WriteUtf8("\xe4");cleared.Clear();cleared.WriteUtf8("ok");
  if(cleared.Snapshot().rows[0]!="ok") return 4;
  ++checked;
  TerminalSessionCore invalid(2,40);invalid.WriteUtf8("\xe4");invalid.WriteUtf8("\x1b[31mX");
  auto bad=invalid.Snapshot();
  if(bad.cells[0][0].text!="\xef\xbf\xbd" || bad.cells[0][1].text!="X" || bad.cells[0][1].style.foreground!=1) return 5;
  ++checked;
  TerminalSessionCore noncanonical(2,40);noncanonical.WriteUtf8("\xc0\xaf\xed\xa0\x80\xf4\x90\x80\x80Z");
  auto rejected=noncanonical.Snapshot();
  if(rejected.rows[0].find("\xc0")!=std::string::npos || rejected.rows[0].find("\xed")!=std::string::npos || rejected.rows[0].find("\xf4")!=std::string::npos || rejected.rows[0].back()!='Z') return 6;
  ++checked;
  std::cout << "PASS stream UTF-8 cases=" << checked << '\n';
}
