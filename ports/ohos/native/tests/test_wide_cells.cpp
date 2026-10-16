#include "terminal_session_core.h"
#include <iostream>
using teraterm::ohos::TerminalSessionCore;
int main() {
  int cases=0;
  auto check=[&](bool ok,const char* label){if(!ok){std::cerr<<"FAIL "<<label<<'\n';return false;}++cases;return true;};
  auto valid=[](const TerminalSessionCore& s){auto v=s.Snapshot();for(const auto& row:v.cells)for(std::size_t c=0;c<row.size();++c){if(row[c].columns==0&&(c==0||row[c-1].columns!=2))return false;if(row[c].columns==2&&(c+1==row.size()||row[c+1].columns!=0))return false;}return true;};
  TerminalSessionCore cjk(2,12);cjk.WriteUtf8("A\xe4\xb8\xad" "B");auto a=cjk.Snapshot();
  if(!check(a.rows[0]=="A\xe4\xb8\xad" "B"&&a.cursor_col==4&&a.cells[0][1].columns==2&&a.cells[0][2].columns==0&&valid(cjk),"CJK occupies two columns"))return 1;
  TerminalSessionCore emoji(2,12);emoji.WriteUtf8("\xf0\x9f\x8c\x8aX");a=emoji.Snapshot();
  if(!check(a.cursor_col==3&&a.cells[0][0].columns==2&&valid(emoji),"emoji two columns"))return 1;
  TerminalSessionCore wrap(2,4);wrap.WriteUtf8("abc\xe4\xb8\xadX");a=wrap.Snapshot();
  if(!check(a.rows[0]=="abc"&&a.rows[1]=="\xe4\xb8\xadX"&&a.cursor_col==3&&valid(wrap),"wide glyph wraps atomically"))return 1;
  TerminalSessionCore edge(2,4);edge.WriteUtf8("ab\xe4\xb8\xadX");a=edge.Snapshot();
  if(!check(a.rows[0]=="ab\xe4\xb8\xad"&&a.rows[1]=="X"&&valid(edge),"wide glyph fits final pair"))return 1;
  TerminalSessionCore overwrite(1,6);overwrite.WriteUtf8("\xe4\xb8\xadQ\x1b[1;2HX");a=overwrite.Snapshot();
  if(!check(a.rows[0]==" XQ"&&valid(overwrite),"overwrite continuation clears pair"))return 1;
  TerminalSessionCore erase(1,6);erase.WriteUtf8("\xe4\xb8\xadX\x1b[1;2H\x1b[K");
  if(!check(erase.Snapshot().rows[0].empty()&&valid(erase),"erase continuation clears pair"))return 1;
  TerminalSessionCore resize(1,6);resize.WriteUtf8("ab\xe4\xb8\xad");resize.Resize(1,3);
  if(!check(resize.Snapshot().rows[0]=="ab"&&valid(resize),"resize removes clipped pair"))return 1;
  TerminalSessionCore insert(1,8);insert.WriteUtf8("\xe4\xb8\xad" "A\x1b[1;1H\x1b[@");
  if(!check(insert.Snapshot().rows[0]==" \xe4\xb8\xad" "A"&&valid(insert),"insert preserves pair"))return 1;
  TerminalSessionCore remove(1,8);remove.WriteUtf8("A\xe4\xb8\xad" "B\x1b[1;1H\x1b[P");
  if(!check(remove.Snapshot().rows[0]=="\xe4\xb8\xad" "B"&&valid(remove),"delete preserves moved pair"))return 1;
  TerminalSessionCore half(1,8);half.WriteUtf8("\xe4\xb8\xad" "B\x1b[1;2H\x1b[P");
  if(!check(half.Snapshot().rows[0]==" B"&&valid(half),"delete partial pair clears atomically"))return 1;
  TerminalSessionCore small(1,1);small.WriteUtf8("\xe4\xb8\xad");
  if(!check(small.Snapshot().rows[0]=="\xef\xbf\xbd"&&valid(small),"one column fallback"))return 1;
  TerminalSessionCore style(1,6);style.WriteUtf8("\x1b[31m\xe4\xb8\xad\x1b[0mX");a=style.Snapshot();
  if(!check(a.cells[0][0].style.foreground==1&&a.cells[0][1].style.foreground==1&&a.cells[0][2].style.foreground==7&&valid(style),"wide color and next narrow cell"))return 1;
  std::cout<<"PASS wide-cell cases="<<cases<<'\n';return 0;
}
