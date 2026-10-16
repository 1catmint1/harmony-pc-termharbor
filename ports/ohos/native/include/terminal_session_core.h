#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace teraterm::ohos {

struct TerminalCellStyle {
  int foreground = 7;
  int background = 0;
  bool bold = false;
  bool underline = false;
  bool inverse = false;
};

struct TerminalCell {
  std::string text = " ";
  unsigned columns = 1; // 2: wide head, 0: its continuation, 1: ordinary cell.
  TerminalCellStyle style;
};

struct TerminalSnapshot {
  std::vector<std::string> scrollback_rows;
  std::vector<std::string> rows;
  std::vector<std::vector<TerminalCell>> cells;
  std::size_t cursor_row = 0;
  std::size_t cursor_col = 0;
};

class TerminalSessionCore {
public:
  TerminalSessionCore(std::size_t rows, std::size_t cols);

  void Resize(std::size_t rows, std::size_t cols);
  void WriteUtf8(const std::string& bytes);
  void Clear();

  TerminalSnapshot Snapshot() const;
  std::size_t Rows() const { return rows_; }
  std::size_t Cols() const { return cols_; }

private:
  enum class EscapeState {
    None,
    Escape,
    Csi,
  };

  void PutGlyph(const std::string& glyph);
  void PutSpace();
  void NewLine();
  void ScrollUp();
  void AppendScrollbackRow(const std::string& row_text);
  void DispatchByte(unsigned char ch);
  void DispatchCsi(char final_byte);
  void ApplySgr(const std::vector<int>& params);
  void ResetStyle();
  void SaveCursor();
  void RestoreCursor();
  void InsertBlankCells(std::size_t row, std::size_t col, std::size_t count);
  void DeleteCells(std::size_t row, std::size_t col, std::size_t count);
  void InsertBlankLines(std::size_t row, std::size_t count);
  void DeleteLines(std::size_t row, std::size_t count);
  void ScrollDown(std::size_t count);
  void ClearRow(std::size_t row);
  void ClearCell(std::size_t row, std::size_t col);
  void RepairRow(std::size_t row);
  std::vector<int> CsiParams(int default_value) const;
  std::string RowText(std::size_t row) const;
  TerminalCell& Cell(std::size_t row, std::size_t col);
  const TerminalCell& Cell(std::size_t row, std::size_t col) const;

  std::size_t rows_;
  std::size_t cols_;
  std::size_t cursor_row_ = 0;
  std::size_t cursor_col_ = 0;
  std::size_t saved_cursor_row_ = 0;
  std::size_t saved_cursor_col_ = 0;
  bool pending_wrap_ = false;
  EscapeState escape_state_ = EscapeState::None;
  std::string csi_buffer_;
  // A transport read may end in the middle of a Unicode scalar.
  std::string pending_utf8_;
  TerminalCellStyle current_style_;
  std::vector<TerminalCell> cells_;
  std::vector<std::string> scrollback_rows_;
};

}  // namespace teraterm::ohos
