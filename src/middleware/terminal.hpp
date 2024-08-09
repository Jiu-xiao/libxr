#pragma once

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "ramfs.hpp"
#include "stack.hpp"
#include <cctype>
#include <cstdio>

namespace LibXR {
class Terminal {
public:
  enum class Mode { CRLF = 0, LF = 1, CR = 2, NONE = 3 };

  template <Mode MODE = Mode::CRLF, int READ_BUFF_SIZE = 32,
            int WRITE_BUFF_SIZE = 128, int MAX_LINE_SIZE = READ_BUFF_SIZE>
  Terminal(LibXR::RamFS &ramfs, RamFS::Dir *current_dir = NULL,
           WriteOperation write_op = WriteOperation(),
           ReadPort &read_port = STDIO::read,
           WritePort &write_port = STDIO::write)
      : mode_(MODE), ramfs_(ramfs), write_op_(write_op), read_(read_port),
        write_(write_port), input_line_(MAX_LINE_SIZE),
        read_buff_size_(READ_BUFF_SIZE), write_buff_size_(WRITE_BUFF_SIZE) {
    read_buff_ = *(new char[READ_BUFF_SIZE]);
    write_buff_ = *(new char[WRITE_BUFF_SIZE]);
    if (current_dir == NULL) {
      current_dir_ = &ramfs_.root_;
    }
  }

  Mode mode_;
  WriteOperation write_op_;
  ReadPort &read_;
  WritePort &write_;
  RamFS &ramfs_;
  RawData read_buff_;
  const size_t read_buff_size_;
  RawData write_buff_;
  const size_t write_buff_size_;
  Stack<uint8_t> input_line_;

  RamFS::Dir *current_dir_;

  uint8_t flag_ansi = 1;

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

  void DisplayChar(char data) {
    if (input_line_.Push(data) == ErrorCode::OK) {
      write_(write_op_, ConstRawData(data));
    }
  }

  void DeleteChar() {
    if (input_line_.Pop() == ErrorCode::OK) {
      write_(write_op_, ConstRawData("\b \b", 3));
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
          break;
        case 'B':
          break;
        case 'C':
          break;
        case 'D':
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
          LineFeed();
          ShowHeader();
          input_line_.Reset();
        }
        break;
      case '\r':
        if (mode_ == Mode::LF) {
          LineFeed();
          ShowHeader();
          input_line_.Reset();
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
    buff.size_ = MIN(MAX(1, term->read_.Size()), term->read_buff_size_);
    static ReadOperation op(UINT32_MAX);
    while (true) {
      if (term->read_(op, buff) == ErrorCode::OK) {
        term->Prase(buff);
      }
    }
  }

  static void TaskFun(Terminal *term) {
    RawData buff = term->read_buff_;
    buff.size_ = MIN(MAX(1, term->read_.Size()), term->read_buff_size_);

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
