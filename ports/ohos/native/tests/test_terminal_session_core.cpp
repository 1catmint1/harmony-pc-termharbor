#include "terminal_session_core.h"

#include <cassert>
#include <string>

using teraterm::ohos::TerminalSessionCore;

int main() {
  TerminalSessionCore session(3, 10);
  session.WriteUtf8("hello\r\nworld");
  auto snapshot = session.Snapshot();
  assert(snapshot.rows[0] == "hello");
  assert(snapshot.rows[1] == "world");

  session.WriteUtf8("\x1b[1;1HHi");
  snapshot = session.Snapshot();
  assert(snapshot.rows[0] == "Hillo");

  session.WriteUtf8("\x1b[2J");
  snapshot = session.Snapshot();
  assert(snapshot.rows[0].empty());
  assert(snapshot.rows[1].empty());
  assert(snapshot.cursor_row == 0);
  assert(snapshot.cursor_col == 0);

  session.WriteUtf8("abcdefghi");
  session.WriteUtf8("\bZ");
  snapshot = session.Snapshot();
  assert(snapshot.rows[0] == "abcdefghZ");

  session.Resize(2, 5);
  session.WriteUtf8("\x1b[2;1Hend");
  snapshot = session.Snapshot();
  assert(snapshot.rows[1] == "end");

  TerminalSessionCore styled(2, 12);
  styled.WriteUtf8("ok \x1b[31;1mred\x1b[0m plain");
  snapshot = styled.Snapshot();
  assert(snapshot.rows[0] == "ok red plain");
  assert(snapshot.cells[0][3].text == "r");
  assert(snapshot.cells[0][3].style.foreground == 1);
  assert(snapshot.cells[0][3].style.bold);
  assert(snapshot.cells[0][7].text == "p");
  assert(snapshot.cells[0][7].style.foreground == 7);
  assert(!snapshot.cells[0][7].style.bold);

  TerminalSessionCore wrap(2, 5);
  wrap.WriteUtf8("abcdef");
  snapshot = wrap.Snapshot();
  assert(snapshot.rows[0] == "abcde");
  assert(snapshot.rows[1] == "f");
  assert(snapshot.cursor_row == 1);
  assert(snapshot.cursor_col == 1);
  wrap.WriteUtf8("\r\nline2\r\nline3");
  snapshot = wrap.Snapshot();
  assert(!snapshot.scrollback_rows.empty());
  assert(snapshot.scrollback_rows[0] == "abcde");
  wrap.WriteUtf8("\x1b[3J");
  snapshot = wrap.Snapshot();
  assert(snapshot.scrollback_rows.empty());

  TerminalSessionCore erase(1, 8);
  erase.WriteUtf8("12345678\x1b[1;4H\x1b[K");
  snapshot = erase.Snapshot();
  assert(snapshot.rows[0] == "123");

  TerminalSessionCore cursor(2, 10);
  cursor.WriteUtf8("abc\x1b[s\x1b[2;5HX\x1b[uY");
  snapshot = cursor.Snapshot();
  assert(snapshot.rows[0] == "abcY");
  assert(snapshot.rows[1] == "    X");

  TerminalSessionCore edit(1, 8);
  edit.WriteUtf8("abcde\x1b[1;3H\x1b[2@XY");
  snapshot = edit.Snapshot();
  assert(snapshot.rows[0] == "abXYcde");
  edit.WriteUtf8("\x1b[1;3H\x1b[2P");
  snapshot = edit.Snapshot();
  assert(snapshot.rows[0] == "abcde");

  TerminalSessionCore lines(4, 8);
  lines.WriteUtf8("one\r\ntwo\r\nthree\r\nfour");
  lines.WriteUtf8("\x1b[2;1H\x1b[L");
  snapshot = lines.Snapshot();
  assert(snapshot.rows[0] == "one");
  assert(snapshot.rows[1].empty());
  assert(snapshot.rows[2] == "two");
  assert(snapshot.rows[3] == "three");
  lines.WriteUtf8("\x1b[2;1H\x1b[M");
  snapshot = lines.Snapshot();
  assert(snapshot.rows[1] == "two");
  assert(snapshot.rows[2] == "three");
  assert(snapshot.rows[3].empty());

  TerminalSessionCore column(1, 8);
  column.WriteUtf8("ab\x1b[5GZ");
  snapshot = column.Snapshot();
  assert(snapshot.rows[0] == "ab  Z");
  return 0;
}
