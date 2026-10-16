#include "terminal_session_core.h"
#include "unicode_width_16.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace teraterm::ohos {
namespace {

constexpr std::size_t kDefaultRows = 24;
constexpr std::size_t kDefaultCols = 80;
constexpr std::size_t kMaxScrollbackRows = 10000;
constexpr int kTabWidth = 8;

std::size_t ClampDimension(std::size_t value, std::size_t fallback) {
  return value == 0 ? fallback : value;
}

std::string TrimRight(std::string value) {
  while (!value.empty() && value.back() == ' ') {
    value.pop_back();
  }
  return value;
}

}  // namespace

TerminalSessionCore::TerminalSessionCore(std::size_t rows, std::size_t cols)
    : rows_(ClampDimension(rows, kDefaultRows)),
      cols_(ClampDimension(cols, kDefaultCols)),
      cells_(rows_ * cols_) {}

void TerminalSessionCore::Resize(std::size_t rows, std::size_t cols) {
  rows = ClampDimension(rows, kDefaultRows);
  cols = ClampDimension(cols, kDefaultCols);

  std::vector<TerminalCell> next(rows * cols);
  const std::size_t copy_rows = std::min(rows_, rows);
  const std::size_t copy_cols = std::min(cols_, cols);
  for (std::size_t row = 0; row < copy_rows; ++row) {
    for (std::size_t col = 0; col < copy_cols; ++col) {
      next[row * cols + col] = Cell(row, col);
    }
  }

  rows_ = rows;
  cols_ = cols;
  cells_ = std::move(next);
  for (std::size_t row = 0; row < rows_; ++row) RepairRow(row);
  cursor_row_ = std::min(cursor_row_, rows_ - 1);
  cursor_col_ = std::min(cursor_col_, cols_ - 1);
  pending_wrap_ = false;
}

void TerminalSessionCore::WriteUtf8(const std::string& bytes) {
  const std::string input = pending_utf8_ + bytes;
  pending_utf8_.clear();
  for (std::size_t index = 0; index < input.size();) {
    const auto lead = static_cast<unsigned char>(input[index]);
    if (lead < 0x80) { DispatchByte(lead); ++index; continue; }
    const std::size_t count = lead >= 0xc2 && lead <= 0xdf ? 2 :
      lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
    bool valid = count != 0;
    for (std::size_t part = 1; valid && part < count && index + part < input.size(); ++part) {
      const auto next = static_cast<unsigned char>(input[index + part]);
      valid = next >= 0x80 && next <= 0xbf;
      if (part == 1) valid = valid && !(lead == 0xe0 && next < 0xa0) &&
        !(lead == 0xed && next >= 0xa0) && !(lead == 0xf0 && next < 0x90) &&
        !(lead == 0xf4 && next >= 0x90);
    }
    if (!valid) {
      if (escape_state_ == EscapeState::None) PutGlyph("\xef\xbf\xbd");
      ++index;
      continue;
    }
    if (input.size() - index < count) {
      pending_utf8_ = input.substr(index); // At most three bytes, independently per session.
      break;
    }
    if (escape_state_ == EscapeState::None) PutGlyph(input.substr(index, count));
    index += count;
  }
}

void TerminalSessionCore::Clear() {
  std::fill(cells_.begin(), cells_.end(), TerminalCell{});
  cursor_row_ = 0;
  cursor_col_ = 0;
  pending_wrap_ = false;
  escape_state_ = EscapeState::None;
  csi_buffer_.clear();
  pending_utf8_.clear();
}

TerminalSnapshot TerminalSessionCore::Snapshot() const {
  TerminalSnapshot snapshot;
  snapshot.cursor_row = cursor_row_;
  snapshot.cursor_col = cursor_col_;
  snapshot.scrollback_rows = scrollback_rows_;
  snapshot.rows.reserve(rows_);
  snapshot.cells.reserve(rows_);
  for (std::size_t row = 0; row < rows_; ++row) {
    snapshot.rows.push_back(RowText(row));
    std::vector<TerminalCell> row_cells;
    row_cells.reserve(cols_);
    for (std::size_t col = 0; col < cols_; ++col) {
      row_cells.push_back(Cell(row, col));
    }
    snapshot.cells.push_back(std::move(row_cells));
  }
  return snapshot;
}

void TerminalSessionCore::PutGlyph(const std::string& glyph) {
  std::uint32_t scalar = static_cast<unsigned char>(glyph[0]);
  if (glyph.size() > 1) {
    scalar &= glyph.size() == 2 ? 0x1f : glyph.size() == 3 ? 0x0f : 0x07;
    for (std::size_t i = 1; i < glyph.size(); ++i) scalar = (scalar << 6) | (static_cast<unsigned char>(glyph[i]) & 0x3f);
  }
  const unsigned width = IsWideScalar(scalar) ? 2 : 1;
  if (width > cols_) { PutGlyph("\xef\xbf\xbd"); return; }
  if (pending_wrap_ || cursor_col_ + width > cols_) {
    NewLine();
  }
  ClearCell(cursor_row_, cursor_col_);
  if (width == 2) ClearCell(cursor_row_, cursor_col_ + 1);
  TerminalCell& cell = Cell(cursor_row_, cursor_col_);
  cell.text = glyph;
  cell.columns = width;
  cell.style = current_style_;
  if (width == 2) {
    auto& continuation = Cell(cursor_row_, cursor_col_ + 1);
    continuation.text.clear(); continuation.columns = 0; continuation.style = current_style_;
  }
  if (cursor_col_ + width >= cols_) {
    cursor_col_ = cols_ - 1;
    pending_wrap_ = true;
  } else {
    cursor_col_ += width;
  }
}

void TerminalSessionCore::PutSpace() {
  PutGlyph(" ");
}

void TerminalSessionCore::NewLine() {
  cursor_col_ = 0;
  pending_wrap_ = false;
  if (cursor_row_ + 1 >= rows_) {
    ScrollUp();
  } else {
    ++cursor_row_;
  }
}

void TerminalSessionCore::ScrollUp() {
  AppendScrollbackRow(RowText(0));
  if (rows_ <= 1) {
    std::fill(cells_.begin(), cells_.end(), TerminalCell{});
    return;
  }
  for (std::size_t row = 1; row < rows_; ++row) {
    for (std::size_t col = 0; col < cols_; ++col) {
      Cell(row - 1, col) = Cell(row, col);
    }
  }
  for (std::size_t col = 0; col < cols_; ++col) {
    ClearCell(rows_ - 1, col);
  }
}

void TerminalSessionCore::AppendScrollbackRow(const std::string& row_text) {
  if (row_text.empty()) {
    return;
  }
  scrollback_rows_.push_back(row_text);
  if (scrollback_rows_.size() > kMaxScrollbackRows) {
    scrollback_rows_.erase(
        scrollback_rows_.begin(),
        scrollback_rows_.begin() + static_cast<std::ptrdiff_t>(scrollback_rows_.size() - kMaxScrollbackRows));
  }
}

void TerminalSessionCore::DispatchByte(unsigned char ch) {
  if (escape_state_ == EscapeState::Escape) {
    if (ch == '[') {
      escape_state_ = EscapeState::Csi;
      csi_buffer_.clear();
    } else if (ch == '7') {
      SaveCursor();
      escape_state_ = EscapeState::None;
    } else if (ch == '8') {
      RestoreCursor();
      escape_state_ = EscapeState::None;
    } else {
      escape_state_ = EscapeState::None;
    }
    return;
  }

  if (escape_state_ == EscapeState::Csi) {
    if (ch >= 0x40 && ch <= 0x7e) {
      DispatchCsi(static_cast<char>(ch));
      escape_state_ = EscapeState::None;
      csi_buffer_.clear();
    } else {
      csi_buffer_.push_back(static_cast<char>(ch));
    }
    return;
  }

  switch (ch) {
    case '\x1b':
      escape_state_ = EscapeState::Escape;
      break;
    case '\r':
      cursor_col_ = 0;
      pending_wrap_ = false;
      break;
    case '\n':
      NewLine();
      break;
    case '\b':
      pending_wrap_ = false;
      if (cursor_col_ > 0) {
        --cursor_col_;
      }
      break;
    case '\t': {
      const std::size_t target =
          std::min(cols_ - 1, ((cursor_col_ / kTabWidth) + 1) * kTabWidth);
      while (cursor_col_ < target) {
        PutSpace();
      }
      break;
    }
    default:
      if (std::isprint(ch) != 0) {
        PutGlyph(std::string(1, static_cast<char>(ch)));
      }
      break;
  }
}

void TerminalSessionCore::DispatchCsi(char final_byte) {
  switch (final_byte) {
    case 'A': {
      const std::vector<int> params = CsiParams(1);
      const int first = params.empty() ? 1 : params[0];
      cursor_row_ -= std::min<std::size_t>(cursor_row_, std::max(first, 1));
      pending_wrap_ = false;
      break;
    }
    case 'B': {
      const std::vector<int> params = CsiParams(1);
      const int first = params.empty() ? 1 : params[0];
      cursor_row_ = std::min(rows_ - 1, cursor_row_ + std::max(first, 1));
      pending_wrap_ = false;
      break;
    }
    case 'C': {
      const std::vector<int> params = CsiParams(1);
      const int first = params.empty() ? 1 : params[0];
      cursor_col_ = std::min(cols_ - 1, cursor_col_ + std::max(first, 1));
      pending_wrap_ = false;
      break;
    }
    case 'D': {
      const std::vector<int> params = CsiParams(1);
      const int first = params.empty() ? 1 : params[0];
      cursor_col_ -= std::min<std::size_t>(cursor_col_, std::max(first, 1));
      pending_wrap_ = false;
      break;
    }
    case 'G': {
      const std::vector<int> params = CsiParams(1);
      const int first = params.empty() ? 1 : params[0];
      cursor_col_ = std::min(cols_ - 1, static_cast<std::size_t>(std::max(first, 1) - 1));
      pending_wrap_ = false;
      break;
    }
    case 'd': {
      const std::vector<int> params = CsiParams(1);
      const int first = params.empty() ? 1 : params[0];
      cursor_row_ = std::min(rows_ - 1, static_cast<std::size_t>(std::max(first, 1) - 1));
      pending_wrap_ = false;
      break;
    }
    case 'H':
    case 'f': {
      const std::vector<int> params = CsiParams(1);
      const int first = params.empty() ? 1 : params[0];
      const int second = params.size() > 1 ? params[1] : 1;
      cursor_row_ = std::min(rows_ - 1, static_cast<std::size_t>(std::max(first, 1) - 1));
      cursor_col_ = std::min(cols_ - 1, static_cast<std::size_t>(std::max(second, 1) - 1));
      pending_wrap_ = false;
      break;
    }
    case 'J': {
      const std::vector<int> params = CsiParams(0);
      const int mode = params.empty() ? 0 : params[0];
      if (mode == 2) {
        Clear();
      } else if (mode == 3) {
        scrollback_rows_.clear();
      } else if (mode == 0) {
        for (std::size_t col = cursor_col_; col < cols_; ++col) {
          ClearCell(cursor_row_, col);
        }
        for (std::size_t row = cursor_row_ + 1; row < rows_; ++row) {
          ClearRow(row);
        }
      } else if (mode == 1) {
        for (std::size_t row = 0; row < cursor_row_; ++row) {
          ClearRow(row);
        }
        for (std::size_t col = 0; col <= cursor_col_; ++col) {
          ClearCell(cursor_row_, col);
        }
      }
      break;
    }
    case 'K': {
      const std::vector<int> params = CsiParams(0);
      const int mode = params.empty() ? 0 : params[0];
      if (mode == 0) {
        for (std::size_t col = cursor_col_; col < cols_; ++col) {
          ClearCell(cursor_row_, col);
        }
      } else if (mode == 1) {
        for (std::size_t col = 0; col <= cursor_col_; ++col) {
          ClearCell(cursor_row_, col);
        }
      } else if (mode == 2) {
        ClearRow(cursor_row_);
      }
      break;
    }
    case '@': {
      const std::vector<int> params = CsiParams(1);
      const int first = params.empty() ? 1 : params[0];
      InsertBlankCells(cursor_row_, cursor_col_, static_cast<std::size_t>(std::max(first, 1)));
      pending_wrap_ = false;
      break;
    }
    case 'P': {
      const std::vector<int> params = CsiParams(1);
      const int first = params.empty() ? 1 : params[0];
      DeleteCells(cursor_row_, cursor_col_, static_cast<std::size_t>(std::max(first, 1)));
      pending_wrap_ = false;
      break;
    }
    case 'X': {
      const std::vector<int> params = CsiParams(1);
      const int first = params.empty() ? 1 : params[0];
      const std::size_t count =
          std::min(cols_ - cursor_col_, static_cast<std::size_t>(std::max(first, 1)));
      for (std::size_t col = 0; col < count; ++col) {
        ClearCell(cursor_row_, cursor_col_ + col);
      }
      pending_wrap_ = false;
      break;
    }
    case 'L': {
      const std::vector<int> params = CsiParams(1);
      const int first = params.empty() ? 1 : params[0];
      InsertBlankLines(cursor_row_, static_cast<std::size_t>(std::max(first, 1)));
      pending_wrap_ = false;
      break;
    }
    case 'M': {
      const std::vector<int> params = CsiParams(1);
      const int first = params.empty() ? 1 : params[0];
      DeleteLines(cursor_row_, static_cast<std::size_t>(std::max(first, 1)));
      pending_wrap_ = false;
      break;
    }
    case 'S': {
      const std::vector<int> params = CsiParams(1);
      const int first = params.empty() ? 1 : params[0];
      for (int index = 0; index < std::max(first, 1); ++index) {
        ScrollUp();
      }
      pending_wrap_ = false;
      break;
    }
    case 'T': {
      const std::vector<int> params = CsiParams(1);
      const int first = params.empty() ? 1 : params[0];
      ScrollDown(static_cast<std::size_t>(std::max(first, 1)));
      pending_wrap_ = false;
      break;
    }
    case 's':
      SaveCursor();
      break;
    case 'u':
      RestoreCursor();
      break;
    case 'm':
      ApplySgr(CsiParams(0));
      break;
    default:
      break;
  }
}

void TerminalSessionCore::ApplySgr(const std::vector<int>& params) {
  if (params.empty()) {
    ResetStyle();
    return;
  }

  for (const int param : params) {
    if (param == 0) {
      ResetStyle();
    } else if (param == 1) {
      current_style_.bold = true;
    } else if (param == 4) {
      current_style_.underline = true;
    } else if (param == 7) {
      current_style_.inverse = true;
    } else if (param == 22) {
      current_style_.bold = false;
    } else if (param == 24) {
      current_style_.underline = false;
    } else if (param == 27) {
      current_style_.inverse = false;
    } else if (param >= 30 && param <= 37) {
      current_style_.foreground = param - 30;
    } else if (param == 39) {
      current_style_.foreground = 7;
    } else if (param >= 40 && param <= 47) {
      current_style_.background = param - 40;
    } else if (param == 49) {
      current_style_.background = 0;
    } else if (param >= 90 && param <= 97) {
      current_style_.foreground = 8 + (param - 90);
    } else if (param >= 100 && param <= 107) {
      current_style_.background = 8 + (param - 100);
    }
  }
}

void TerminalSessionCore::ResetStyle() {
  current_style_ = TerminalCellStyle{};
}

void TerminalSessionCore::SaveCursor() {
  saved_cursor_row_ = cursor_row_;
  saved_cursor_col_ = cursor_col_;
}

void TerminalSessionCore::RestoreCursor() {
  cursor_row_ = std::min(saved_cursor_row_, rows_ - 1);
  cursor_col_ = std::min(saved_cursor_col_, cols_ - 1);
  pending_wrap_ = false;
}

void TerminalSessionCore::InsertBlankCells(std::size_t row, std::size_t col, std::size_t count) {
  if (row >= rows_ || col >= cols_ || count == 0) {
    return;
  }
  count = std::min(count, cols_ - col);
  if (Cell(row, col).columns == 0) ClearCell(row, col);
  std::vector<TerminalCell> next(cols_);
  for (std::size_t target = 0; target < col; ++target) next[target] = Cell(row, target);
  for (std::size_t target = col + count; target < cols_; ++target) next[target] = Cell(row, target - count);
  for (std::size_t target = 0; target < cols_; ++target) Cell(row, target) = std::move(next[target]);
  RepairRow(row);
}

void TerminalSessionCore::DeleteCells(std::size_t row, std::size_t col, std::size_t count) {
  if (row >= rows_ || col >= cols_ || count == 0) {
    return;
  }
  count = std::min(count, cols_ - col);
  if (Cell(row, col).columns == 0) ClearCell(row, col);
  if (col + count < cols_ && Cell(row, col + count).columns == 0) ClearCell(row, col + count);
  std::vector<TerminalCell> next(cols_);
  for (std::size_t target = 0; target < col; ++target) next[target] = Cell(row, target);
  for (std::size_t target = col; target + count < cols_; ++target) next[target] = Cell(row, target + count);
  for (std::size_t target = 0; target < cols_; ++target) Cell(row, target) = std::move(next[target]);
  RepairRow(row);
}

void TerminalSessionCore::InsertBlankLines(std::size_t row, std::size_t count) {
  if (row >= rows_ || count == 0) {
    return;
  }
  count = std::min(count, rows_ - row);
  for (std::size_t target = rows_; target-- > row;) {
    if (target >= row + count) {
      for (std::size_t col = 0; col < cols_; ++col) {
        Cell(target, col) = Cell(target - count, col);
      }
    } else {
      ClearRow(target);
    }
  }
}

void TerminalSessionCore::DeleteLines(std::size_t row, std::size_t count) {
  if (row >= rows_ || count == 0) {
    return;
  }
  count = std::min(count, rows_ - row);
  for (std::size_t target = row; target < rows_; ++target) {
    if (target + count < rows_) {
      for (std::size_t col = 0; col < cols_; ++col) {
        Cell(target, col) = Cell(target + count, col);
      }
    } else {
      ClearRow(target);
    }
  }
}

void TerminalSessionCore::ScrollDown(std::size_t count) {
  InsertBlankLines(0, count);
}

void TerminalSessionCore::ClearRow(std::size_t row) {
  for (std::size_t col = 0; col < cols_; ++col) {
    ClearCell(row, col);
  }
}

void TerminalSessionCore::ClearCell(std::size_t row, std::size_t col) {
  const auto width = Cell(row, col).columns;
  if (width == 0 && col > 0 && Cell(row, col - 1).columns == 2) Cell(row, col - 1) = TerminalCell{};
  if (width == 2 && col + 1 < cols_ && Cell(row, col + 1).columns == 0) Cell(row, col + 1) = TerminalCell{};
  Cell(row, col) = TerminalCell{};
}

void TerminalSessionCore::RepairRow(std::size_t row) {
  for (std::size_t col = 0; col < cols_; ++col) {
    auto& cell = Cell(row, col);
    if (cell.columns == 2) {
      if (col + 1 == cols_) { cell = TerminalCell{}; continue; }
      auto& tail = Cell(row, ++col); tail.text.clear(); tail.columns = 0; tail.style = cell.style;
    } else if (cell.columns == 0) cell = TerminalCell{};
  }
}

std::vector<int> TerminalSessionCore::CsiParams(int default_value) const {
  std::vector<int> params;
  std::stringstream stream(csi_buffer_);
  std::string item;
  while (std::getline(stream, item, ';')) {
    if (item.empty()) {
      params.push_back(default_value);
      continue;
    }
    char* end = nullptr;
    const long parsed = std::strtol(item.c_str(), &end, 10);
    params.push_back(end == item.c_str() ? default_value : static_cast<int>(parsed));
  }
  if (params.empty()) {
    params.push_back(default_value);
  }
  return params;
}

std::string TerminalSessionCore::RowText(std::size_t row) const {
  std::string value;
  for (std::size_t col = 0; col < cols_; ++col) {
    value += Cell(row, col).text;
  }
  return TrimRight(value);
}

TerminalCell& TerminalSessionCore::Cell(std::size_t row, std::size_t col) {
  return cells_[row * cols_ + col];
}

const TerminalCell& TerminalSessionCore::Cell(std::size_t row, std::size_t col) const {
  return cells_[row * cols_ + col];
}

}  // namespace teraterm::ohos
