#pragma once

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_string.hpp"
#include "libxr_type.hpp"
#include "queue.hpp"
#include "ramfs.hpp"
#include "stack.hpp"
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

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

  char *strchr_rev(char *str, char c) {
    auto len = strlen(str);
    for (int i = len - 1; i >= 0; i--) {
      if (str[i] == c) {
        return str + i;
      }
    }
    return NULL;
  }

public:
  enum class Mode { CRLF = 0, LF = 1, CR = 2, NONE = 3 };

  Terminal(LibXR::RamFS &ramfs, RamFS::Dir *current_dir = NULL,
           WriteOperation write_op = WriteOperation(),
           ReadPort &read_port = STDIO::read,
           WritePort &write_port = STDIO::write, Mode MODE = Mode::CRLF)
      : mode_(MODE), ramfs_(ramfs), write_op_(write_op), read_(read_port),
        write_(write_port), input_line_(MAX_LINE_SIZE + 1),
        history_(MAX_HISTORY_NUMBER) {
    if (current_dir == NULL) {
      current_dir_ = &ramfs_.root_;
    }
  }

  const Mode mode_;
  WriteOperation write_op_;
  ReadPort &read_;
  WritePort &write_;
  RamFS &ramfs_;
  char read_buff_[READ_BUFF_SIZE];

  RamFS::Dir *current_dir_;
  uint8_t flag_ansi_ = 1;
  int offset_ = 0;
  Stack<char> input_line_;
  char *arg_tab_[MAX_ARG_NUMBER];
  size_t arg_number_ = 0;
  Queue<LibXR::String<MAX_LINE_SIZE>> history_;
  int history_index_ = -1;

  void LineFeed() {
    if (mode_ == Mode::CRLF) {
      write_(write_op_, ConstRawData("\r\n"));
    } else if (mode_ == Mode::LF) {
      write_(write_op_, ConstRawData('\n'));
    } else if (mode_ == Mode::CR) {
      write_(write_op_, ConstRawData('\r'));
    }
  }

  void UpdateDisplayPosition() {
    write_(write_op_, ConstRawData(KEY_SAVE, sizeof(KEY_SAVE) - 1));
    write_(write_op_, ConstRawData(CLEAR_BEHIND, sizeof(CLEAR_BEHIND) - 1));
    write_(write_op_,
           ConstRawData(&input_line_[input_line_.Size() + offset_], -offset_));
    write_(write_op_, ConstRawData(KEY_LOAD, sizeof(KEY_LOAD) - 1));
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
    if (CanDisplayChar()) {
      write_(write_op_, ConstRawData(data));
      AddCharToInputLine(data);
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
    if (history_index_ >= 0) {
      CopyHistoryToInputLine();
      ShowHistory();
    }
    if (CanDeleteChar()) {
      write_(write_op_, ConstRawData(DELECT_CHAR, sizeof(DELECT_CHAR) - 1));
      RemoveCharFromInputLine();
    }
  }

  void ShowHeaderRecursively(RamFS::Dir *dir) {
    if (dir != current_dir_) {
      ShowHeaderRecursively(reinterpret_cast<RamFS::Dir *>(dir->parent));
    }
    if (dir != &ramfs_.root_) {
      write_(write_op_, ConstRawData(dir->data_.name, strlen(dir->data_.name)));
      write_(write_op_, ConstRawData('/'));
    }
  }

  void ShowHeader() {
    RamFS::Dir *dir = current_dir_;

    write_(write_op_,
           ConstRawData(ramfs_.root_->name, strlen(ramfs_.root_->name)));
    write_(write_op_, ConstRawData(":/", 2));

    ShowHeaderRecursively(dir);

    write_(write_op_, ConstRawData("$ ", 2));
  }

  void ClearLine() {
    write_(write_op_, ConstRawData(CLEAR_LINE, sizeof(CLEAR_LINE) - 1));
  }

  void Clear() {
    write_(write_op_, ConstRawData(CLEAR_ALL, sizeof(CLEAR_ALL) - 1));
  }

  void ShowHistory() {
    ClearLine();
    ShowHeader();
    offset_ = 0;
    if (history_index_ >= 0) {
      write_(write_op_, ConstRawData(history_[-history_index_ - 1].Raw(),
                                     history_[-history_index_ - 1].Length()));
    } else {
      write_(write_op_, ConstRawData(&input_line_[0], input_line_.Size()));
    }
  }

  void CopyHistoryToInputLine() {
    input_line_.Reset();
    for (int i = 0; i < history_[-history_index_ - 1].Length(); i++) {
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
    auto len = strlen(path);
    size_t index = 0;
    RamFS::Dir *dir = current_dir_;
    RamFS::File *file;

    if (*path == '/') {
      index++;
      dir = &ramfs_.root_;
    }

    for (int i = 0; i < MAX_LINE_SIZE; i++) {
      auto tmp = strchr(path + index, '/');
      if (tmp == NULL) {
        return dir->FindDir(path + index);
      } else if (tmp == path + index) {
        return NULL;
      } else {
        tmp[0] = '\0';
        dir = dir->FindDir(path + index);
        tmp[0] = '/';
        index += tmp - path + 1;
        if (path[index] == '\0') {
          return dir;
        }
      }
    }

    return NULL;
  }

  RamFS::File *Path2File(char *path) {
    auto name = strchr_rev(path, '/');

    if (name == NULL) {
      return current_dir_->FindFile(path);
    }

    if (name[1] == '\0') {
      return NULL;
    }

    *name = '\0';
    RamFS::Dir *dir = Path2Dir(path);
    *name = '/';
    if (dir != NULL) {
      return dir->FindFile(name + 1);
    } else {
      return NULL;
    }
  }

  void ExecuteCommand() {
    if (arg_number_ < 1 || arg_number_ > MAX_ARG_NUMBER) {
      return;
    }

    AddHistory();

    auto ans = Path2File(arg_tab_[0]);
    if (ans == nullptr) {
      write_(write_op_, ConstRawData("Command not found."));
      LineFeed();
      return;
    }

    if ((*ans)->type != RamFS::FileType::EXEC) {
      write_(write_op_, ConstRawData("Not an executable file."));
      LineFeed();
      return;
    }

    (*ans)->Run(arg_number_, arg_tab_);
  }

  void Parse(RawData &raw_data) {
    char *buff = static_cast<char *>(raw_data.addr_);
    for (int i = 0; i < raw_data.size_; i++) {
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
          write_(write_op_, ConstRawData(KEY_RIGHT, sizeof(KEY_RIGHT) - 1));
        }

        break;
      case 'D':
        if (history_index_ >= 0) {
          CopyHistoryToInputLine();
          ShowHistory();
        }
        if (offset_ + input_line_.Size() > 0) {
          offset_--;
          write_(write_op_, ConstRawData(KEY_LEFT, sizeof(KEY_LEFT) - 1));
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
    while (*path == ' ' && *path != '\0') {
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
    if (tmp - &input_line_[0] != input_line_.Size() + offset_) {
      return;
    }

    /* get start of prefix */
    char *prefix_start = NULL;
    RamFS::Dir *dir = NULL;

    if (path_end == path) {
      dir = current_dir_;
      prefix_start = path_end;
    } else {
      prefix_start = path_end + 1;
    }

    /* find dir*/
    if (dir == NULL) {
      *path_end = '\0';
      dir = Path2Dir(path);
      *path_end = '/';
      if (dir == NULL) {
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

    int prefix_len = tmp - prefix_start;

    MatchResult res = {NULL, 0, prefix_start, prefix_len, this, 0};

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
        result->terminal->write_(result->terminal->write_op_,
                                 ConstRawData(node->name, name_len));
        result->terminal->LineFeed();
        if (result->node == NULL) {
          result->node = &node;
          result->same_char_number = name_len;
          return ErrorCode::OK;
        }

        for (int i = 0; i < name_len; i++) {
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
      for (int i = 0; i < name_len - res.prefix_len; i++) {
        DisplayChar(res.node->data_.name[i + res.prefix_len]);
      }
    } else {
      res.node = NULL;
      LineFeed();
      (*dir)->rbt.Foreach<RamFS::FsNode, MatchResult *>(foreach_fun_show, &res);

      ShowHeader();
      write_(write_op_, ConstRawData(&input_line_[0], input_line_.Size()));

      for (int i = 0; i < res.same_char_number - res.prefix_len; i++) {
        DisplayChar(res.node->data_.name[i + res.prefix_len]);
      }
    }
  }

  void HandleControlCharacter(char data) {
    switch (data) {
    case '\n':
      if (mode_ == Mode::CRLF || mode_ == Mode::LF) {
      parse_data:
        if (history_index_ >= 0) {
          CopyHistoryToInputLine();
        }
        LineFeed();
        if (input_line_.Size() > 0) {
          GetArgs();
          ExecuteCommand();
          arg_number_ = 0;
        }
        ShowHeader();
        input_line_.Reset();
        input_line_[0] = '\0';
        offset_ = 0;
      }
      break;
    case '\r':
      if (mode_ == Mode::LF) {
        goto parse_data;
      }
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
    buff.size_ = MIN(MAX(1, term->read_.Size()), READ_BUFF_SIZE);
    static ReadOperation op(UINT32_MAX);
    while (true) {
      if (term->read_(op, buff) == ErrorCode::OK) {
        term->Parse(buff);
      }
    }
  }

  static void TaskFun(Terminal *term) {
    RawData buff = term->read_buff_;
    buff.size_ = MIN(MAX(1, term->read_.Size()), term->READ_BUFF_SIZE);

    static ReadOperation op(UINT32_MAX);
  check_status:
    switch (term->read_.GetStatus()) {
    case ReadOperation::OperationPollingStatus::READY:
      term->read_(op, buff);
      goto check_status;
    case ReadOperation::OperationPollingStatus::RUNNING:
      break;
    case ReadOperation::OperationPollingStatus::DONE:
      term->Parse(buff);
      break;
    }
  }
};
} // namespace LibXR
