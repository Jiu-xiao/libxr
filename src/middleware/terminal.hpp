#pragma once

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_string.hpp"
#include "libxr_type.hpp"
#include "queue.hpp"
#include "ramfs.hpp"
#include "stack.hpp"
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

static const char CLEAR_ALL[] = "\033[2J\033[1H";
static const char CLEAR_LINE[] = "\033[2K\r";
static const char CLEAR_BEHIND[] = "\033[K";
static const char KEY_RIGHT[] = "\033[C";
static const char KEY_LEFT[] = "\033[D";
static const char KEY_SAVE[] = "\033[s";
static const char KEY_LOAD[] = "\033[u";

namespace LibXR {
template <int READ_BUFF_SIZE = 32, int WRITE_BUFF_SIZE = 128,
          int MAX_LINE_SIZE = READ_BUFF_SIZE, int MAX_ARG_NUMBER = 5,
          int MAX_HISTORY_NUMBER = 5>
class Terminal {
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
  char write_buff_[WRITE_BUFF_SIZE];
  Stack<char> input_line_;
  char *arg_tab_[MAX_ARG_NUMBER];
  size_t arg_number_;
  Queue<LibXR::String<MAX_LINE_SIZE>> history_;
  int history_index_ = -1;

  RamFS::Dir *current_dir_;
  uint8_t flag_ansi = 1;
  int offset_ = 0;

  void LineFeed() {
    switch (mode_) {
    case Mode::CRLF:
      write_(write_op_, ConstRawData(*"\r\n"));
    case Mode::LF:
      write_(write_op_, ConstRawData('\n'));
    case Mode::CR:
      write_(write_op_, ConstRawData('\r'));
    case Mode::NONE:
      break;
    }
  }

  void RefreshOffset() {
    write_(write_op_, ConstRawData(KEY_SAVE, sizeof(KEY_SAVE) - 1));
    write_(write_op_, ConstRawData(CLEAR_BEHIND, sizeof(CLEAR_BEHIND) - 1));
    write_(write_op_,
           ConstRawData(&input_line_[input_line_.Size() + offset_], -offset_));
    write_(write_op_, ConstRawData(KEY_LOAD, sizeof(KEY_LOAD) - 1));
  }

  void DisplayChar(char data) {
    if (history_index_ >= 0) {
      ApplyHistory();
      ShowHistory();
    }

    if (input_line_.EmptySize() > 1) {
      write_(write_op_, ConstRawData(data));
      if (offset_ == 0) {
        input_line_.Push(data);
      } else {
        input_line_.Insert(data, input_line_.Size() + offset_);
        RefreshOffset();
      }
    }
  }

  void DeleteChar() {
    if (history_index_ >= 0) {
      ApplyHistory();
      ShowHistory();
    }

    if (input_line_.Size() + offset_ > 0) {
      write_(write_op_, ConstRawData("\b \b", 3));
      if (offset_ == 0) {
        input_line_.Pop();
      } else {
        input_line_.Delete(input_line_.Size() + offset_ - 1);
        RefreshOffset();
      }
    }
  }

  void _ShowHeader(RamFS::Dir *dir) {
    if (dir != current_dir_) {
      _ShowHeader(reinterpret_cast<RamFS::Dir *>(dir->parent));
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

    _ShowHeader(dir);

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

  void ApplyHistory() {
    input_line_.Reset();
    for (int i = 0; i < history_[-history_index_ - 1].Length(); i++) {
      input_line_.Push(history_[-history_index_ - 1][i]);
    }
    history_index_ = -1;
    offset_ = 0;
  }

  void GetArgs() {
    input_line_.Push('\0');

    if (history_.EmptySize() == 0) {
      history_.Pop();
    }
    history_.Push(*reinterpret_cast<String<MAX_LINE_SIZE> *>(&input_line_[0]));

    auto max_len = input_line_.Size();
    size_t index = 0;
    arg_number_ = 0;

    for (int i = 0; i < max_len; i++) {
      if (input_line_[i] == ' ') {
        input_line_[i] = '\0';
      }
    }

    while (index < max_len && arg_number_ < MAX_ARG_NUMBER) {
      if (input_line_[index] == '\0') {
        index++;
        continue;
      } else {
        arg_tab_[arg_number_++] = &input_line_[index];
        index += strnlen(&input_line_[index], max_len - index);
        continue;
      }
    }
  }

  RamFS::Dir *Path2Dir() {
    // TODO:
    return current_dir_;
  }

  RamFS::File *Path2File(char *path) {
    auto len = strlen(path);
    size_t index = 0;
    RamFS::Dir *dir = current_dir_;
    RamFS::File *file;

    if (*path == '/') {
      index++;
      dir = &ramfs_.root_;
    }

    while (index < len) {
      size_t dir_name_index = index;
      while (path[dir_name_index] != '/') {
        if (path[dir_name_index] == '\0') {
          return dir->FindFile(&path[index]);
        }
        dir_name_index++;
      }

      path[dir_name_index] = '\0';
      dir = dir->FindDir(&path[index]);
      if (!dir) {
        return NULL;
      }

      index = dir_name_index + 1;
    }

    return NULL;
  }

  void RunCommand() {
    if (arg_number_ < 1 || arg_number_ > MAX_ARG_NUMBER) {
      return;
    }

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

  void Prase(RawData &raw_data) {
    char *buff = (char *)raw_data.addr_;
    for (int i = 0; i < raw_data.size_; i++) {
      _Prase(buff[i]);
    }
  }

  void _Prase(char data) {
    if (flag_ansi) {
      if (flag_ansi == 1) {
        if (isprint(data)) {
          flag_ansi++;
        } else {
          flag_ansi = 0;
        }
      } else if (flag_ansi == 2) {
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
        case 'C':
          if (history_index_ >= 0) {
            ApplyHistory();
            ShowHistory();
          }
          if (offset_ < 0) {
            offset_++;
            write_(write_op_, ConstRawData(KEY_RIGHT, sizeof(KEY_RIGHT) - 1));
          }

          break;
        case 'D':
          if (history_index_ >= 0) {
            ApplyHistory();
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

        flag_ansi = 0;
      }
    } else if (isprint(data)) {
      DisplayChar(data);
    } else {
      switch (data) {
      case '\n':
        if (mode_ == Mode::CRLF || mode_ == Mode::LF) {
        prase_data:
          if (history_index_ >= 0) {
            ApplyHistory();
          }
          LineFeed();
          GetArgs();
          RunCommand();
          ShowHeader();
          input_line_.Reset();
          offset_ = 0;
        }
        break;
      case '\r':
        if (mode_ == Mode::LF) {
          goto prase_data;
        }
        break;
      case 0x7f:
        DeleteChar();
        break;
      case '\t':
        break;
      case '\033':
        flag_ansi = 1;
        break;
      default:
        break;
      }
    }
  }

  static void ThreadFun(Terminal *term) {
    RawData buff = term->read_buff_;
    buff.size_ = MIN(MAX(1, term->read_.Size()), READ_BUFF_SIZE);
    static ReadOperation op(UINT32_MAX);
    while (true) {
      if (term->read_(op, buff) == ErrorCode::OK) {
        term->Prase(buff);
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
      term->Prase(buff);
      break;
    }
  }
};
} // namespace LibXR
