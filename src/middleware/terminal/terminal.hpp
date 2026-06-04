#pragma once

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "libxr_rw.hpp"
#include "ramfs.hpp"
#include "semaphore.hpp"
#include "stack.hpp"

namespace LibXR
{

/**
 * @class Terminal
 * @brief  终端类，实现一个基于 RamFS 的基本命令行接口
 *         Terminal class implementing a basic command-line interface based on RamFS
 * @tparam READ_BUFF_SIZE 读取缓冲区大小 Read buffer size
 * @tparam MAX_LINE_SIZE 最大输入行长度 Maximum input line size
 * @tparam MAX_ARG_NUMBER 最大参数数量 Maximum number of arguments
 * @tparam MAX_HISTORY_NUMBER 最大历史记录数量 Maximum number of command history records
 */
template <size_t READ_BUFF_SIZE = 32, size_t MAX_LINE_SIZE = READ_BUFF_SIZE,
          size_t MAX_ARG_NUMBER = 5, size_t MAX_HISTORY_NUMBER = 5>
class Terminal
{
 private:
  /**
   * @brief  一条历史命令的固定存储单元
   *         Fixed storage slot for one history command line
   */
  using HistoryLine = std::array<char, MAX_LINE_SIZE + 1>;

  /**
   * @brief  终端控制序列常量
   *         Terminal control-sequence constants
   *
   * These literals are emitted directly into the write stream to implement
   * clear-screen, clear-line, cursor-save/restore, and simple cursor motion.
   * 这些字面量会被直接写进终端输出流，用来实现清屏、清行、保存/恢复光标和基础光标移动。
   */
  static constexpr char CLEAR_ALL[] =
      "\033[2J\033[1H";  ///< 清屏命令 Clear screen command
  static constexpr char CLEAR_LINE[] =
      "\033[2K\r";  ///< 清除当前行命令 Clear current line command
  static constexpr char CLEAR_BEHIND[] =
      "\033[K";  ///< 清除光标后内容命令 Clear content after cursor command
  static constexpr char KEY_RIGHT[] = "\033[C";  ///< 右箭头键 Right arrow key
  static constexpr char KEY_LEFT[] = "\033[D";   ///< 左箭头键 Left arrow key
  static constexpr char KEY_SAVE[] = "\033[s";   ///< 保存光标位置 Save cursor position
  static constexpr char KEY_LOAD[] = "\033[u";   ///< 恢复光标位置 Restore cursor position
  static constexpr char DELETE_CHAR[] =
      "\b \b";  ///< 退格删除字符 Backspace delete character

  /**
   * @brief  反向查找字符串中的特定字符
   *         Finds a specific character in a string from the end
   * @param  str 输入字符串 Input string
   * @param  c 要查找的字符 Character to find
   * @return char* 指向找到的字符的指针，如果未找到返回 nullptr
   *         Pointer to the found character, nullptr if not found
   */
  char* StrchrRev(char* str, char c)
  {
    auto len = strlen(str);
    for (int i = static_cast<int>(len - 1); i >= 0; i--)
    {
      if (str[i] == c)
      {
        return str + i;
      }
    }
    return nullptr;
  }

 public:
  /**
   * @enum Mode
   * @brief  终端换行模式
   *         Line feed modes for the terminal
   */
  enum class Mode : uint8_t
  {
    CRLF = 0,  ///< 回车换行 Carriage Return + Line Feed (\r\n)
    LF = 1,    ///< 仅换行 Line Feed (\n)
    CR = 2     ///< 仅回车 Carriage Return (\r)
  };

  /**
   * @brief  终端构造函数，初始化文件系统、I/O 端口和当前目录
   *         Constructor to initialize the terminal with file system, I/O ports, and
   * current directory
   * @param  ramfs 关联的 RamFS 文件系统 Associated RamFS file system
   * @param  current_dir 当前目录（默认为根目录）Current directory (default: root)
   * @param  read_port 读取端口（默认使用标准输入）Read port (default: standard input)
   * @param  write_port 写入端口（默认使用标准输出）Write port (default: standard output)
   * @param  MODE 终端换行模式（默认 CRLF）Terminal line feed mode (default: CRLF)
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   */
  Terminal(LibXR::RamFS& ramfs, RamFS::Dir* current_dir = nullptr,
           ReadPort* read_port = STDIO::read_, WritePort* write_port = STDIO::write_,
           Mode MODE = Mode::CRLF)
      : read_status_(ReadOperation::OperationPollingStatus::READY),
        write_status_(WriteOperation::OperationPollingStatus::READY),
        MODE(MODE),
        write_op_(write_status_),
        read_port_(read_port),
        write_port_(write_port),
        write_stream_(write_port_, write_op_),
        ramfs_(ramfs),
        current_dir_(current_dir ? current_dir : &ramfs_.root_),
        input_line_(MAX_LINE_SIZE + 1),
        history_(MAX_HISTORY_NUMBER)
  {
    ASSERT(read_port != nullptr);
    ASSERT(write_port != nullptr);
    ASSERT(read_port->Readable());
    ASSERT(write_port->Writable());

    if (write_port == STDIO::write_)
    {
      if (STDIO::write_mutex_ == nullptr)
      {
        STDIO::write_mutex_ = new LibXR::Mutex();
      }

      write_mutex_ = STDIO::write_mutex_;
      STDIO::write_stream_ = &write_stream_;
    }
    else
    {
      write_mutex_ = new LibXR::Mutex();
    }
  }

  ReadOperation::OperationPollingStatus read_status_;
  WriteOperation::OperationPollingStatus
      write_status_;  ///< 当前读/写轮询状态 / Current polling status of the read / write side.

  const Mode MODE;                  ///< 终端换行模式 Terminal line feed mode
  WriteOperation write_op_;         ///< 终端写操作 Terminal write operation
  ReadPort* read_port_;             ///< 读取端口 Read port
  WritePort* write_port_;           ///< 写入端口 Write port
  WritePort::Stream write_stream_;  ///< 写入流 Write stream

  LibXR::Mutex* write_mutex_ = nullptr;  ///< 写入端口互斥锁 Write port mutex

  RamFS& ramfs_;                    ///< 关联的文件系统 Associated file system
  char read_buff_[READ_BUFF_SIZE];  ///< 读取缓冲区 Read buffer

  size_t request_read_size_ = 0;   ///< 本轮计划读取的字节数 / Byte count requested for the current read attempt.
  RamFS::Dir* current_dir_;        ///< 当前目录 Current directory
  uint8_t flag_ansi_ = 0;          ///< ANSI 控制字符状态 ANSI control character state
  int offset_ = 0;                 ///< 光标偏移 Cursor offset
  Stack<char> input_line_;         ///< 输入行缓冲区 Input line buffer
  char* arg_tab_[MAX_ARG_NUMBER];  ///< 命令参数列表 Command argument list
  size_t arg_number_ = 0;          ///< 参数数量 Number of arguments
  Queue<HistoryLine> history_;     ///< 历史命令 History of commands
  int history_index_ = -1;         ///< 当前历史索引 Current history index
  bool linefeed_flag_ = false;     ///< CRLF 抑制标志 CRLF suppression flag
  char linefeed_char_ = '\0';      ///< 上一个换行字符 Previous line feed character

  /**
   * @brief  行编辑、显示与历史记录片段
   *         Line-editing, display, and history-management fragment
   */
#include "display.hpp"

  /**
   * @brief  路径解析、命令执行与自动补全片段
   *         Path-resolution, command-execution, and auto-completion fragment
   */
#include "command.hpp"

  /**
   * @brief  输入字节解析与 ANSI / 控制字符分发片段
   *         Input-byte parsing plus ANSI / control-character dispatch fragment
   */
#include "input.hpp"

  /**
   * @brief  线程 / 轮询驱动入口片段
   *         Threaded / polling driver-entry fragment
   */
#include "driver.hpp"
};

}  // namespace LibXR
