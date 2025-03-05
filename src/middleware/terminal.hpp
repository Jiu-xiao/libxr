#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "libxr_string.hpp"
#include "ramfs.hpp"
#include "stack.hpp"

volatile static uint64_t time_before, time_diff;

namespace LibXR {
template <int READ_BUFF_SIZE = 32, int MAX_LINE_SIZE = READ_BUFF_SIZE,
          int MAX_ARG_NUMBER = 5, int MAX_HISTORY_NUMBER = 5>
class Terminal {
 private:
  static constexpr char CLEAR_ALL[] = "\033[2J\033[1H";
  static constexpr char CLEAR_LINE[] = "\033[2K\r";
  static constexpr char CLEAR_BEHIND[] = "\033[K";
  static constexpr char KEY_RIGHT[] = "\033[C";
  static constexpr char KEY_LEFT[] = "\033[D";
  static constexpr char KEY_SAVE[] = "\033[s";
  static constexpr char KEY_LOAD[] = "\033[u";
  static constexpr char DELECT_CHAR[] = "\b \b";

  char *StrchrRev(char *str, char c) {
    auto len = strlen(str);
    for (size_t i = len - 1; i >= 0; i--) {
      if (str[i] == c) {
        return str + i;
      }
    }
    return nullptr;
  }

 public:
  enum class Mode : uint8_t { CRLF = 0, LF = 1, CR = 2 };

  Terminal(LibXR::RamFS &ramfs, RamFS::Dir *current_dir = nullptr,
           WriteOperation write_op = WriteOperation(),
           ReadPort *read_port = STDIO::read_,
           WritePort *write_port = STDIO::write_, Mode MODE = Mode::CRLF)
      : MODE(MODE),
        write_op_(std::move(write_op)),
        read_(read_port),
        write_(write_port),
        ramfs_(ramfs),
        current_dir_(current_dir ? current_dir : &ramfs_.root_),
        input_line_(MAX_LINE_SIZE + 1),
        history_(MAX_HISTORY_NUMBER) {}

  const Mode MODE;
  WriteOperation write_op_;
  ReadPort *read_;
  WritePort *write_;
  RamFS &ramfs_;
  char read_buff_[READ_BUFF_SIZE];

  RamFS::Dir *current_dir_;
  uint8_t flag_ansi_ = 0;
  int offset_ = 0;
  Stack<char> input_line_;
  char *arg_tab_[MAX_ARG_NUMBER];
  size_t arg_number_ = 0;
  Queue<LibXR::String<MAX_LINE_SIZE>> history_;
  int history_index_ = -1;
  bool linefeed_flag_ = false;

  void LineFeed() {
    if (MODE == Mode::CRLF) {
      (*write_)(ConstRawData("\r\n"), write_op_);
    } else if (MODE == Mode::LF) {
      (*write_)(ConstRawData('\n'), write_op_);
    } else if (MODE == Mode::CR) {
      (*write_)(ConstRawData('\r'), write_op_);
    }
  }

  void UpdateDisplayPosition() {
    (*write_)(ConstRawData(KEY_SAVE), write_op_);
    (*write_)(ConstRawData(CLEAR_BEHIND), write_op_);
    (*write_)(
        ConstRawData(&input_line_[input_line_.Size() + offset_], -offset_),
        write_op_);
    (*write_)(ConstRawData(KEY_LOAD), write_op_);
  }

  bool CanDisplayChar() { return input_line_.EmptySize() > 1; }
  bool CanDeleteChar() { return input_line_.Size() + offset_ > 0; }

  void AddCharToInputLine(char data) {
    if (offset_ == 0) {
      input_line_.Push(data);
    } else {
      input_line_.Insert(data, input_line_.Size() + offset_);
      UpdateDisplayPosition();
    }
    input_line_[input_line_.Size()] = '\0';
  }

  void DisplayChar(char data) {
    bool use_history = false;

    if (history_index_ >= 0) {
      CopyHistoryToInputLine();
      use_history = true;
    }

    if (CanDisplayChar()) {
      AddCharToInputLine(data);
      if (use_history) {
        ShowHistory();
      } else {
        (*write_)(ConstRawData(input_line_[input_line_.Size() - 1]), write_op_);
      }
    }
  }

  void RemoveCharFromInputLine() {
    if (offset_ == 0) {
      input_line_.Pop();
    } else {
      input_line_.Delete(input_line_.Size() + offset_ - 1);
      UpdateDisplayPosition();
    }
    input_line_[input_line_.Size()] = '\0';
  }

  void DeleteChar() {
    bool use_history = false;

    if (history_index_ >= 0) {
      CopyHistoryToInputLine();
      use_history = true;
    }

    if (CanDeleteChar()) {
      RemoveCharFromInputLine();
      if (use_history) {
        ShowHistory();
      } else {
        (*write_)(ConstRawData(DELECT_CHAR), write_op_);
      }
    }
  }

  void ShowHeader() {
    (*write_)(ConstRawData(ramfs_.root_->name, strlen(ramfs_.root_->name)),
              write_op_);
    if (current_dir_ == &ramfs_.root_) {
      (*write_)(ConstRawData(":/"), write_op_);
    } else {
      (*write_)(ConstRawData(":"), write_op_);
      (*write_)(ConstRawData(current_dir_->data_.name), write_op_);
    }

    (*write_)(ConstRawData("$ "), write_op_);
  }

  void ClearLine() { (*write_)(ConstRawData(CLEAR_LINE), write_op_); }

  void Clear() { (*write_)(ConstRawData(CLEAR_ALL), write_op_); }

  void ShowHistory() {
    ClearLine();
    ShowHeader();
    offset_ = 0;
    if (history_index_ >= 0) {
      (*write_)(ConstRawData(history_[-history_index_ - 1].Raw(),
                             history_[-history_index_ - 1].Length()),
                write_op_);
    } else {
      (*write_)(ConstRawData(&input_line_[0], input_line_.Size()), write_op_);
    }
  }

  void CopyHistoryToInputLine() {
    input_line_.Reset();
    for (size_t i = 0; i < history_[-history_index_ - 1].Length(); i++) {
      input_line_.Push(history_[-history_index_ - 1][i]);
    }
    history_index_ = -1;
    offset_ = 0;
  }

  void AddHistory() {
    input_line_.Push('\0');

    if (history_.EmptySize() == 0) {
      history_.Pop();
    }
    history_.Push(*reinterpret_cast<String<MAX_LINE_SIZE> *>(&input_line_[0]));
  }

  void GetArgs() {
    for (int i = 0; input_line_[i] != '\0'; i++) {
      if (input_line_[i] == ' ') {
        input_line_[i] = '\0';
      } else if (i == 0 || input_line_[i - 1] == '\0') {
        if (arg_number_ >= MAX_ARG_NUMBER) {
          return;
        }
        arg_tab_[arg_number_++] = &input_line_[i];
      }
    }
  }

  RamFS::Dir *Path2Dir(char *path) {
    size_t index = 0;
    RamFS::Dir *dir = current_dir_;

    if (*path == '/') {
      index++;
      dir = &ramfs_.root_;
    }

    for (int i = 0; i < MAX_LINE_SIZE; i++) {
      auto tmp = strchr(path + index, '/');
      if (tmp == nullptr) {
        return dir->FindDir(path + index);
      } else if (tmp == path + index) {
        return nullptr;
      } else {
        tmp[0] = '\0';
        dir = dir->FindDir(path + index);
        tmp[0] = '/';
        index += tmp - path + 1;
        if (path[index] == '\0' || dir == nullptr) {
          return dir;
        }
      }
    }

    return nullptr;
  }

  RamFS::File *Path2File(char *path) {
    auto name = StrchrRev(path, '/');

    if (name == nullptr) {
      return current_dir_->FindFile(path);
    }

    if (name[1] == '\0') {
      return nullptr;
    }

    *name = '\0';
    RamFS::Dir *dir = Path2Dir(path);
    *name = '/';
    if (dir != nullptr) {
      return dir->FindFile(name + 1);
    } else {
      return nullptr;
    }
  }

  void ExecuteCommand() {
    AddHistory();

    GetArgs();

    if (arg_number_ < 1 || arg_number_ > MAX_ARG_NUMBER) {
      return;
    }

    if (strcmp(arg_tab_[0], "cd") == 0) {
      RamFS::Dir *dir = Path2Dir(arg_tab_[1]);
      if (dir != nullptr) {
        current_dir_ = dir;
      }
      LineFeed();
      return;
    }

    if (strcmp(arg_tab_[0], "ls") == 0) {
      ErrorCode (*ls_fun)(RBTree<const char *>::Node<RamFS::FsNode> &,
                          Terminal *) =
          [](RBTree<const char *>::Node<RamFS::FsNode> &item, Terminal *term) {
            switch (item->type) {
              case RamFS::FsNodeType::DIR:
                (*(term->write_))(ConstRawData("d "), term->write_op_);
                break;
              case RamFS::FsNodeType::FILE:
                (*(term->write_))(ConstRawData("f "), term->write_op_);
                break;
              case RamFS::FsNodeType::DEVICE:
                (*(term->write_))(ConstRawData("c "), term->write_op_);
                break;
              case RamFS::FsNodeType::STORAGE:
                (*(term->write_))(ConstRawData("b "), term->write_op_);
                break;
              default:
                (*(term->write_))(ConstRawData("? "), term->write_op_);
                break;
            }
            (*(term->write_))(ConstRawData(item.data_.name), term->write_op_);
            term->LineFeed();
            return ErrorCode::OK;
          };

      current_dir_->data_.rbt.Foreach<RamFS::FsNode>(ls_fun, this);
      return;
    }

    auto ans = Path2File(arg_tab_[0]);
    if (ans == nullptr) {
      (*write_)(ConstRawData("Command not found."), write_op_);
      LineFeed();
      return;
    }

    if ((*ans)->type != RamFS::FileType::EXEC) {
      (*write_)(ConstRawData("Not an executable file."), write_op_);
      LineFeed();
      return;
    }

    (*ans)->Run(arg_number_, arg_tab_);
  }

  void Parse(RawData &raw_data) {
    char *buff = static_cast<char *>(raw_data.addr_);
    for (size_t i = 0; i < raw_data.size_; i++) {
      HandleCharacter(buff[i]);
    }
  }

  void HandleAnsiCharacter(char data) {
    if (flag_ansi_ == 1) {
      if (isprint(data)) {
        flag_ansi_++;
      } else {
        flag_ansi_ = 0;
      }
    } else if (flag_ansi_ == 2) {
      switch (data) {
        case 'A':
          if (history_index_ < int(history_.Size()) - 1) {
            history_index_++;
            ShowHistory();
          }
          break;
        case 'B':
          if (history_index_ >= 0) {
            history_index_--;
            ShowHistory();
          }
          break;
        case 'C':
          if (history_index_ >= 0) {
            CopyHistoryToInputLine();
            ShowHistory();
          }
          if (offset_ < 0) {
            offset_++;
            (*write_)(ConstRawData(KEY_RIGHT, sizeof(KEY_RIGHT) - 1),
                      write_op_);
          }

          break;
        case 'D':
          if (history_index_ >= 0) {
            CopyHistoryToInputLine();
            ShowHistory();
          }
          if (offset_ + input_line_.Size() > 0) {
            offset_--;
            (*write_)(ConstRawData(KEY_LEFT, sizeof(KEY_LEFT) - 1), write_op_);
          }
          break;
        default:
          break;
      }

      flag_ansi_ = 0;
    }
  }

  void AutoComplete() {
    /* skip space */
    char *path = &input_line_[0];
    while (*path == ' ') {
      path++;
    }

    /* find last '/' in first argument */
    char *tmp = path, *path_end = path;

    while (*tmp != ' ' && *tmp != '\0') {
      if (*tmp == '/') {
        path_end = tmp;
      }
      tmp++;
    }

    /* return if not need complete */
    if (tmp - &input_line_[0] !=
        static_cast<int>(input_line_.Size() + offset_)) {
      return;
    }

    /* get start of prefix */
    char *prefix_start = nullptr;
    RamFS::Dir *dir = nullptr;

    if (path_end == path) {
      dir = current_dir_;
      prefix_start = path_end;
    } else {
      prefix_start = path_end + 1;
    }

    /* find dir*/
    if (dir == nullptr) {
      *path_end = '\0';
      dir = Path2Dir(path);
      *path_end = '/';
      if (dir == nullptr) {
        return;
      }
    }

    /* prepre for match */
    typedef struct {
      RBTree<const char *>::Node<RamFS::FsNode> *node;
      uint32_t number;
      char *prefix;
      int prefix_len;
      Terminal *terminal;
      size_t same_char_number;
    } MatchResult;

    if (*prefix_start == '/') {
      prefix_start++;
    }

    int prefix_len = static_cast<int>(tmp - prefix_start);

    MatchResult res = {nullptr, 0, prefix_start, prefix_len, this, 0};

    auto foreach_fun_find = [](RBTree<const char *>::Node<RamFS::FsNode> &node,
                               MatchResult *result) {
      if (strncmp(node->name, result->prefix, result->prefix_len) == 0) {
        result->node = &node;
        result->number++;
      }

      return ErrorCode::OK;
    };

    auto foreach_fun_show = [](RBTree<const char *>::Node<RamFS::FsNode> &node,
                               MatchResult *result) {
      if (strncmp(node->name, result->prefix, result->prefix_len) == 0) {
        auto name_len = strlen(node->name);
        (*result->terminal->write_)(ConstRawData(node->name, name_len),
                                    result->terminal->write_op_);
        result->terminal->LineFeed();
        if (result->node == nullptr) {
          result->node = &node;
          result->same_char_number = name_len;
          return ErrorCode::OK;
        }

        for (size_t i = 0; i < name_len; i++) {
          if (node->name[i] != result->node->data_.name[i]) {
            result->same_char_number = i;
            break;
          }
        }

        if (result->same_char_number > name_len) {
          name_len = result->same_char_number;
        }

        result->node = &node;
      }

      return ErrorCode::OK;
    };

    /* start match */
    (*dir)->rbt.Foreach<RamFS::FsNode, MatchResult *>(foreach_fun_find, &res);

    if (res.number == 0) {
      return;
    } else if (res.number == 1) {
      auto name_len = strlen(res.node->data_.name);
      for (size_t i = 0; i < name_len - res.prefix_len; i++) {
        DisplayChar(res.node->data_.name[i + res.prefix_len]);
      }
    } else {
      res.node = nullptr;
      LineFeed();
      (*dir)->rbt.Foreach<RamFS::FsNode, MatchResult *>(foreach_fun_show, &res);

      ShowHeader();
      (*write_)(ConstRawData(&input_line_[0], input_line_.Size()), write_op_);

      for (size_t i = 0; i < res.same_char_number - res.prefix_len; i++) {
        DisplayChar(res.node->data_.name[i + res.prefix_len]);
      }
    }
  }

  void HandleControlCharacter(char data) {
    if (data != '\r' && data != '\n') {
      linefeed_flag_ = false;
    }

    switch (data) {
      case '\n':
      case '\r':
        if (linefeed_flag_) {
          linefeed_flag_ = false;
          return;
        }
        if (history_index_ >= 0) {
          CopyHistoryToInputLine();
        }
        LineFeed();
        if (input_line_.Size() > 0) {
          ExecuteCommand();
          arg_number_ = 0;
        }
        ShowHeader();
        input_line_.Reset();
        input_line_[0] = '\0';
        offset_ = 0;
        break;
      case 0x7f:
        DeleteChar();
        break;
      case '\t':
        AutoComplete();
        break;
      case '\033':
        flag_ansi_ = 1;
        break;
      default:
        break;
    }
  }

  void HandleCharacter(char data) {
    if (flag_ansi_) {
      HandleAnsiCharacter(data);
    } else if (isprint(data)) {
      DisplayChar(data);
    } else {
      HandleControlCharacter(data);
    }
  }

  static void ThreadFun(Terminal *term) {
    RawData buff = term->read_buff_;
    buff.size_ = LibXR::min(LibXR::max(1, term->read_->Size()), READ_BUFF_SIZE);
    static ReadOperation op(UINT32_MAX);
    while (true) {
      if ((*term->read_)(buff, op) == ErrorCode::OK) {
        term->Parse(buff);
      }
    }
  }

  static void TaskFun(Terminal *term) {
    RawData buff = term->read_buff_;
    buff.size_ = LibXR::min(LibXR::max(1, term->read_->Size()), READ_BUFF_SIZE);

    static ReadOperation op;

    while (true) {
      switch (term->read_->GetStatus()) {
        case ReadOperation::OperationPollingStatus::READY:
          (*term->read_)(buff, op);
          continue;
        case ReadOperation::OperationPollingStatus::RUNNING:
          return;
        case ReadOperation::OperationPollingStatus::DONE:
          term->Parse(buff);
          return;
      }
    }
  }
};
}  // namespace LibXR
