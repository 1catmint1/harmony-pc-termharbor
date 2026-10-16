#pragma once

#include "profile_importer.h"
#include "ssh_transport.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace teraterm::ohos {

class TerminalSession;

enum class MacroStatus {
  Running,
  Waiting,
  Completed,
  Error,
};

struct MacroOptions {
  std::string known_hosts_text;
  KnownHostPolicy known_host_policy = KnownHostPolicy::AcceptNew;
  int connect_timeout_ms = 5000;
};

struct MacroStepResult {
  MacroStatus status = MacroStatus::Running;
  std::size_t next_line = 0;
  int wait_ms = 0;
  std::string message;
  std::string updated_known_hosts_text;
  std::string dialog_type;
  std::string dialog_title;
  std::string dialog_text;
  std::string dialog_default_text;
  std::vector<std::string> dialog_items;
  int dialog_selected_index = -1;
  bool dialog_password = false;
  bool dialog_save = false;
  std::string status_box_action;
  std::string status_box_title;
  std::string status_box_text;
};

class MacroRunner {
public:
  MacroRunner(const std::string& script, MacroOptions options);
  ~MacroRunner();

  MacroStepResult Advance(TerminalSession* session, std::size_t max_commands = 100);
  bool RespondDialog(bool accepted, const std::string& value);
  bool IsDone() const { return done_; }
  std::size_t NextLine() const { return next_line_; }

private:
  enum class WaitKind {
    None,
    Text,
    Pause,
    Dialog,
  };

  struct WaitState {
    WaitKind kind = WaitKind::None;
    bool regex = false;
    bool deadline_set = false;
    bool dialog_answer_ready = false;
    bool dialog_accepted = false;
    bool dialog_password = false;
    bool dialog_save = false;
    int dialog_selected_index = -1;
    std::vector<std::string> texts;
    std::vector<std::string> dialog_items;
    std::string dialog_type;
    std::string dialog_title;
    std::string dialog_text;
    std::string dialog_default_text;
    std::string dialog_value;
    std::chrono::steady_clock::time_point deadline;
  };

  struct MacroValue {
    bool is_string = false;
    int int_value = 0;
    std::string string_value;
  };

  struct FileHandle {
    std::unique_ptr<std::fstream> stream;
    std::streampos marked_position = 0;
    std::string filename;
    int lock_fd = -1;
    bool locked = false;
  };

  struct ForFrame {
    std::size_t line_index = 0;
    std::string variable;
    int last = 0;
  };

  struct DirectoryHandle {
    std::vector<std::string> names;
    std::size_t next_index = 0;
  };

  struct LogState {
    std::unique_ptr<std::ofstream> stream;
    std::string filename;
    bool binary = false;
    bool append = false;
    bool plain_text = false;
    bool timestamp = false;
    bool hide_dialog = false;
    bool include_screen_buffer = false;
    bool paused = false;
    bool rotate_enabled = false;
    bool auto_close = false;
    int timestamp_type = 0;
    std::uintmax_t rotate_size = 0;
    int rotate_count = 0;
    std::size_t history_offset = 0;
  };

  void BuildLabels();
  MacroStepResult ExecuteLine(TerminalSession* session, const std::string& line);
  MacroStepResult CheckWait(TerminalSession* session);
  MacroStepResult Connect(TerminalSession* session, const std::string& spec);
  MacroStepResult ExecuteIf(TerminalSession* session, const std::string& line);
  MacroStepResult ExecuteWhile(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteUntil(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteDo(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteLoop(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteFor(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteNext(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteBreak(bool continue_loop);
  MacroStepResult ExecuteDim(const std::vector<std::string>& tokens, bool string_array);
  MacroStepResult ExecuteStrCompare(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteStrScan(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteStrCopy(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteStrMatch(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteStrReplace(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteSprintf(const std::vector<std::string>& tokens, bool explicit_destination);
  MacroStepResult ExecuteCall(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteReturn(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteFileCopy(const std::vector<std::string>& tokens, bool concat);
  MacroStepResult ExecuteFileCreate(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteFileDelete(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteFileOpen(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteFileRead(const std::vector<std::string>& tokens, bool line_mode);
  MacroStepResult ExecuteFileRename(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteFileSearch(const std::vector<std::string>& tokens, bool folder_only);
  MacroStepResult ExecuteFileSeek(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteFileMarkPtr(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteFileStat(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteFileStrSeek(const std::vector<std::string>& tokens, bool reverse);
  MacroStepResult ExecuteFileTruncate(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteFileWrite(const std::vector<std::string>& tokens, bool line_mode);
  MacroStepResult ExecuteFileClose(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteFileLock(const std::vector<std::string>& tokens, bool unlock);
  MacroStepResult ExecuteFolderCreate(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteFolderDelete(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteFindFirst(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteFindNext(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteFindClose(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteGetDir(const std::vector<std::string>& tokens, bool tera_term_dir);
  MacroStepResult ExecuteSetDir(const std::vector<std::string>& tokens, bool transfer_dir);
  MacroStepResult ExecuteGetEnv(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteExpandEnv(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteGetDateTime(const std::vector<std::string>& tokens, bool date_mode);
  MacroStepResult ExecuteUptime(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteLogOpen(TerminalSession* session, const std::vector<std::string>& tokens);
  MacroStepResult ExecuteLogClose(TerminalSession* session, const std::vector<std::string>& tokens);
  MacroStepResult ExecuteLogWrite(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteLogControl(TerminalSession* session,
                                    const std::vector<std::string>& tokens,
                                    bool pause);
  MacroStepResult ExecuteLogInfo(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteLogRotate(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteLogAutoCloseMode(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteDialog(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteFileNameBox(const std::vector<std::string>& tokens, bool directory_mode);
  MacroStepResult ExecuteListBox(const std::vector<std::string>& tokens);
  MacroStepResult ExecuteStatusBox(const std::vector<std::string>& tokens, bool close);
  bool SkipToConditionalBranch(std::string* error);
  bool SkipToEndIf(std::string* error);
  bool SkipToEndWhile(std::string* error);
  bool JumpBackToWhile(std::string* error);
  bool SkipToEndUntil(std::string* error);
  bool JumpBackToUntil(std::string* error);
  bool SkipToLoop(std::string* error);
  bool JumpBackToDo(std::string* error);
  bool MoveToNearestLoopBoundary(bool after_boundary, std::string* boundary_command, std::string* error);
  bool JumpToLabel(const std::string& label, std::string* error);
  bool AssignLine(const std::string& line, std::string* error);
  bool EvaluateCondition(const std::string& expression);
  MacroValue EvaluateValue(const std::string& expression);
  int EvaluateInteger(const std::string& expression) const;
  std::string EvaluateToken(const std::string& token);
  std::string JoinEvaluatedArguments(const std::vector<std::string>& tokens,
                                     std::size_t begin,
                                     const std::string& delimiter);
  std::filesystem::path ResolvePath(const std::string& value) const;
  std::filesystem::path ResolveTransferPath(const std::string& value) const;
  void FlushLogOutput(TerminalSession* session);
  std::string PrepareLogData(const std::string& bytes) const;
  void RotateLogIfNeeded();
  void CloseAutoLogIfDone(TerminalSession* session);
  void ReleaseFileLock(FileHandle* handle);
  bool FormatString(const std::string& format,
                    const std::vector<std::string>& tokens,
                    std::size_t argument_index,
                    std::string* output,
                    int* result_code);
  void SetInteger(const std::string& name, int value);
  void SetString(const std::string& name, const std::string& value);
  void ClearGroupMatchStrings();
  void SetRegexMatchVariables(const std::smatch& match);
  bool SetValue(const std::string& target, const MacroValue& value, std::string* error);
  bool ResolveArrayValue(const std::string& token, MacroValue* value) const;
  int WaitTimeoutMs() const;
  void AttachPendingUpdates(MacroStepResult* result);
  void SetCompleted(MacroStepResult* result, const std::string& message = "");
  void SetError(MacroStepResult* result, const std::string& message);
  void SetWaiting(MacroStepResult* result, const std::string& message, int wait_ms);
  void AttachDialog(MacroStepResult* result) const;

  std::vector<std::string> lines_;
  MacroOptions options_;
  std::unordered_map<std::string, std::size_t> labels_;
  std::unordered_map<std::string, MacroValue> variables_;
  std::unordered_map<std::string, std::vector<int>> int_arrays_;
  std::unordered_map<std::string, std::vector<std::string>> string_arrays_;
  std::unordered_map<int, FileHandle> files_;
  std::unordered_map<int, DirectoryHandle> directory_handles_;
  std::vector<ForFrame> for_stack_;
  std::vector<std::size_t> call_stack_;
  LogState log_;
  bool log_auto_close_mode_ = false;
  std::string pending_updated_known_hosts_text_;
  std::string pending_status_box_action_;
  std::string pending_status_box_title_;
  std::string pending_status_box_text_;
  std::string working_directory_;
  std::string transfer_directory_;
  std::string tera_term_directory_;
  std::size_t next_line_ = 0;
  int next_file_handle_ = 1;
  int next_directory_handle_ = 1;
  bool done_ = false;
  WaitState wait_;
};

std::string MacroStatusName(MacroStatus status);

}  // namespace teraterm::ohos
