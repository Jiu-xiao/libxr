#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "libxr_rw.hpp"
#include "libxr_string.hpp"
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
  char *StrchrRev(char *str, char c)
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
   */
  Terminal(LibXR::RamFS &ramfs, RamFS::Dir *current_dir = nullptr,
           ReadPort *read_port = STDIO::read_, WritePort *write_port = STDIO::write_,
           Mode MODE = Mode::CRLF)
      : MODE(MODE),
        write_op_(write_status_),
        read_(read_port),
        write_(write_port),
        ramfs_(ramfs),
        current_dir_(current_dir ? current_dir : &ramfs_.root_),
        input_line_(MAX_LINE_SIZE + 1),
        history_(MAX_HISTORY_NUMBER)
  {
  }

  const Mode MODE;                  ///< 终端换行模式 Terminal line feed mode
  WriteOperation write_op_;         ///< 终端写操作 Terminal write operation
  ReadPort *read_;                  ///< 读取端口 Read port
  WritePort *write_;                ///< 写入端口 Write port
  RamFS &ramfs_;                    ///< 关联的文件系统 Associated file system
  char read_buff_[READ_BUFF_SIZE];  ///< 读取缓冲区 Read buffer

  RamFS::Dir *current_dir_;        ///< 当前目录 Current directory
  uint8_t flag_ansi_ = 0;          ///< ANSI 控制字符状态 ANSI control character state
  int offset_ = 0;                 ///< 光标偏移 Cursor offset
  Stack<char> input_line_;         ///< 输入行缓冲区 Input line buffer
  char *arg_tab_[MAX_ARG_NUMBER];  ///< 命令参数列表 Command argument list
  size_t arg_number_ = 0;          ///< 参数数量 Number of arguments
  Queue<LibXR::String<MAX_LINE_SIZE>> history_;  ///< 历史命令 History of commands
  int history_index_ = -1;                       ///< 当前历史索引 Current history index
  bool linefeed_flag_ = false;                   ///< 换行标志 Line feed flag

  ReadOperation::OperationPollingStatus read_status_ =
      ReadOperation::OperationPollingStatus::READY;
  WriteOperation::OperationPollingStatus write_status_ =
      WriteOperation::OperationPollingStatus::READY;

  /**
   * @brief  执行换行操作
   *         Performs a line feed operation
   */
  void LineFeed()
  {
    if (MODE == Mode::CRLF)
    {
      (*write_)(ConstRawData("\r\n"), write_op_);
    }
    else if (MODE == Mode::LF)
    {
      (*write_)(ConstRawData('\n'), write_op_);
    }
    else if (MODE == Mode::CR)
    {
      (*write_)(ConstRawData('\r'), write_op_);
    }
  }

  /**
   * @brief  更新光标位置
   *         Updates cursor position
   */
  void UpdateDisplayPosition()
  {
    (*write_)(ConstRawData(KEY_SAVE), write_op_);
    (*write_)(ConstRawData(CLEAR_BEHIND), write_op_);
    (*write_)(ConstRawData(&input_line_[input_line_.Size() + offset_], -offset_),
              write_op_);
    (*write_)(ConstRawData(KEY_LOAD), write_op_);
  }

  /**
   * @brief  检查是否可以显示字符
   *         Checks if a character can be displayed
   * @return bool 是否可以显示字符 Whether the character can be displayed
   */
  bool CanDisplayChar() { return input_line_.EmptySize() > 1; }

  /**
   * @brief  检查是否可以删除字符
   *         Checks if a character can be deleted
   * @return bool 是否可以删除字符 Whether the character can be deleted
   */
  bool CanDeleteChar() { return input_line_.Size() + offset_ > 0; }

  /**
   * @brief  向输入行中添加字符，支持在光标位置插入
   *         Adds a character to the input line, supports insertion at the cursor position
   * @param  data 要添加的字符 The character to add
   */
  void AddCharToInputLine(char data)
  {
    if (offset_ == 0)
    {
      input_line_.Push(data);
    }
    else
    {
      input_line_.Insert(data, input_line_.Size() + offset_);
      UpdateDisplayPosition();
    }
    input_line_[input_line_.Size()] = '\0';
  }

  /**
   * @brief  在终端上显示字符，并根据历史记录模式进行相应操作
   *         Displays a character on the terminal and updates accordingly if history mode
   * is active
   * @param  data 要显示的字符 The character to display
   */
  void DisplayChar(char data)
  {
    bool use_history = false;

    if (history_index_ >= 0)
    {
      CopyHistoryToInputLine();
      use_history = true;
    }

    if (CanDisplayChar())
    {
      AddCharToInputLine(data);
      if (use_history)
      {
        ShowHistory();
      }
      else
      {
        (*write_)(ConstRawData(input_line_[input_line_.Size() - 1]), write_op_);
      }
    }
  }

  /**
   * @brief  从输入行中删除字符，支持在光标位置删除
   *         Removes a character from the input line, supports deletion at the cursor
   * position
   */
  void RemoveCharFromInputLine()
  {
    if (offset_ == 0)
    {
      input_line_.Pop();
    }
    else
    {
      input_line_.Delete(input_line_.Size() + offset_ - 1);
      UpdateDisplayPosition();
    }
    input_line_[input_line_.Size()] = '\0';
  }

  /**
   * @brief  处理删除字符操作，支持回退删除，并在历史模式下更新显示
   *         Handles the delete character operation, supports backspace deletion, and
   * updates display in history mode
   */
  void DeleteChar()
  {
    bool use_history = false;

    if (history_index_ >= 0)
    {
      CopyHistoryToInputLine();
      use_history = true;
    }

    if (CanDeleteChar())
    {
      RemoveCharFromInputLine();
      if (use_history)
      {
        ShowHistory();
      }
      else
      {
        (*write_)(ConstRawData(DELETE_CHAR), write_op_);
      }
    }
  }

  /**
   * @brief  显示终端提示符，包括当前目录信息
   *         Displays the terminal prompt, including the current directory information
   */
  void ShowHeader()
  {
    (*write_)(ConstRawData(ramfs_.root_->name, strlen(ramfs_.root_->name)), write_op_);
    if (current_dir_ == &ramfs_.root_)
    {
      (*write_)(ConstRawData(":/"), write_op_);
    }
    else
    {
      (*write_)(ConstRawData(":"), write_op_);
      (*write_)(ConstRawData(current_dir_->data_.name), write_op_);
    }

    (*write_)(ConstRawData("$ "), write_op_);
  }

  /**
   * @brief  清除当前行
   *         Clears the current line
   */
  void ClearLine() { (*write_)(ConstRawData(CLEAR_LINE), write_op_); }

  /**
   * @brief  清除整个终端屏幕
   *         Clears the entire terminal screen
   */
  void Clear() { (*write_)(ConstRawData(CLEAR_ALL), write_op_); }

  /**
   * @brief  显示历史记录中的输入行，更新终端显示
   *         Displays the input line from history and updates the terminal display
   */
  void ShowHistory()
  {
    ClearLine();
    ShowHeader();
    offset_ = 0;
    if (history_index_ >= 0)
    {
      (*write_)(ConstRawData(history_[-history_index_ - 1].Raw(),
                             history_[-history_index_ - 1].Length()),
                write_op_);
    }
    else
    {
      (*write_)(ConstRawData(&input_line_[0], input_line_.Size()), write_op_);
    }
  }

  /**
   * @brief  将历史命令复制到输入行，并重置历史索引和光标偏移
   *         Copies the history command to the input line and resets history index and
   * cursor offset
   */
  void CopyHistoryToInputLine()
  {
    input_line_.Reset();
    for (size_t i = 0; i < history_[-history_index_ - 1].Length(); i++)
    {
      input_line_.Push(history_[-history_index_ - 1][i]);
    }
    history_index_ = -1;
    offset_ = 0;
  }

  /**
   * @brief  将当前输入行添加到历史记录
   *         Adds the current input line to the history
   */
  void AddHistory()
  {
    input_line_.Push('\0');

    if (history_.EmptySize() == 0)
    {
      history_.Pop();
    }
    history_.Push(*reinterpret_cast<String<MAX_LINE_SIZE> *>(&input_line_[0]));
  }

  /**
   * @brief  解析输入行，将其拆分为参数数组
   *         Parses the input line and splits it into argument array
   */
  void GetArgs()
  {
    for (int i = 0; input_line_[i] != '\0'; i++)
    {
      if (input_line_[i] == ' ')
      {
        input_line_[i] = '\0';
      }
      else if (i == 0 || input_line_[i - 1] == '\0')
      {
        if (arg_number_ >= MAX_ARG_NUMBER)
        {
          return;
        }
        arg_tab_[arg_number_++] = &input_line_[i];
      }
    }
  }

  /**
   * @brief  将路径字符串解析为目录对象
   *         Converts a path string into a directory object
   * @param  path 目录路径字符串 The directory path string
   * @return RamFS::Dir* 解析出的目录指针，若找不到则返回 nullptr
   *         Pointer to the resolved directory, or nullptr if not found
   */
  RamFS::Dir *Path2Dir(char *path)
  {
    size_t index = 0;
    RamFS::Dir *dir = current_dir_;

    if (*path == '/')
    {
      index++;
      dir = &ramfs_.root_;
    }

    for (size_t i = 0; i < MAX_LINE_SIZE; i++)
    {
      auto tmp = strchr(path + index, '/');
      if (tmp == nullptr)
      {
        return dir->FindDir(path + index);
      }
      else if (tmp == path + index)
      {
        return nullptr;
      }
      else
      {
        tmp[0] = '\0';
        dir = dir->FindDir(path + index);
        tmp[0] = '/';
        index += tmp - path + 1;
        if (path[index] == '\0' || dir == nullptr)
        {
          return dir;
        }
      }
    }

    return nullptr;
  }

  /**
   * @brief  将路径字符串解析为文件对象
   *         Converts a path string into a file object
   * @param  path 文件路径字符串 The file path string
   * @return RamFS::File* 解析出的文件指针，若找不到则返回 nullptr
   *         Pointer to the resolved file, or nullptr if not found
   */
  RamFS::File *Path2File(char *path)
  {
    auto name = StrchrRev(path, '/');

    if (name == nullptr)
    {
      return current_dir_->FindFile(path);
    }

    if (name[1] == '\0')
    {
      return nullptr;
    }

    *name = '\0';
    RamFS::Dir *dir = Path2Dir(path);
    *name = '/';
    if (dir != nullptr)
    {
      return dir->FindFile(name + 1);
    }
    else
    {
      return nullptr;
    }
  }

  /**
   * @brief  解析并执行输入的命令
   *         Parses and executes the entered command
   */
  void ExecuteCommand()
  {
    AddHistory();

    GetArgs();

    if (arg_number_ < 1 || arg_number_ > MAX_ARG_NUMBER)
    {
      return;
    }

    if (strcmp(arg_tab_[0], "cd") == 0)
    {
      RamFS::Dir *dir = Path2Dir(arg_tab_[1]);
      if (dir != nullptr)
      {
        current_dir_ = dir;
      }
      LineFeed();
      return;
    }

    if (strcmp(arg_tab_[0], "ls") == 0)
    {
      auto ls_fun = [&](RBTree<const char *>::Node<RamFS::FsNode> &item)
      {
        switch (item->type)
        {
          case RamFS::FsNodeType::DIR:
            (*(this->write_))(ConstRawData("d "), this->write_op_);
            break;
          case RamFS::FsNodeType::FILE:
            (*(this->write_))(ConstRawData("f "), this->write_op_);
            break;
          case RamFS::FsNodeType::DEVICE:
            (*(this->write_))(ConstRawData("c "), this->write_op_);
            break;
          case RamFS::FsNodeType::STORAGE:
            (*(this->write_))(ConstRawData("b "), this->write_op_);
            break;
          default:
            (*(this->write_))(ConstRawData("? "), this->write_op_);
            break;
        }
        (*(this->write_))(ConstRawData(item.data_.name), this->write_op_);
        this->LineFeed();
        return ErrorCode::OK;
      };

      current_dir_->data_.rbt.Foreach<RamFS::FsNode>(ls_fun);
      return;
    }

    auto ans = Path2File(arg_tab_[0]);
    if (ans == nullptr)
    {
      (*write_)(ConstRawData("Command not found."), write_op_);
      LineFeed();
      return;
    }

    if ((*ans)->type != RamFS::FileType::EXEC)
    {
      (*write_)(ConstRawData("Not an executable file."), write_op_);
      LineFeed();
      return;
    }

    (*ans)->Run(arg_number_, arg_tab_);
  }

  /**
   * @brief  解析输入数据流，将其转换为字符并处理
   *         Parses the input data stream, converting it into characters and processing
   * them
   * @param  raw_data 输入的原始数据 Input raw data
   */
  void Parse(RawData &raw_data)
  {
    char *buff = static_cast<char *>(raw_data.addr_);
    for (size_t i = 0; i < raw_data.size_; i++)
    {
      HandleCharacter(buff[i]);
    }
  }

  /**
   * @brief  解析输入数据流，将其转换为字符并处理
   *         Parses the input data stream, converting it into characters and processing
   * them
   * @param  raw_data 输入的原始数据 Input raw data
   */
  void HandleAnsiCharacter(char data)
  {
    if (flag_ansi_ == 1)
    {
      if (isprint(data))
      {
        flag_ansi_++;
      }
      else
      {
        flag_ansi_ = 0;
      }
    }
    else if (flag_ansi_ == 2)
    {
      switch (data)
      {
        case 'A':
          if (history_index_ < int(history_.Size()) - 1)
          {
            history_index_++;
            ShowHistory();
          }
          break;
        case 'B':
          if (history_index_ >= 0)
          {
            history_index_--;
            ShowHistory();
          }
          break;
        case 'C':
          if (history_index_ >= 0)
          {
            CopyHistoryToInputLine();
            ShowHistory();
          }
          if (offset_ < 0)
          {
            offset_++;
            (*write_)(ConstRawData(KEY_RIGHT, sizeof(KEY_RIGHT) - 1), write_op_);
          }

          break;
        case 'D':
          if (history_index_ >= 0)
          {
            CopyHistoryToInputLine();
            ShowHistory();
          }
          if (offset_ + input_line_.Size() > 0)
          {
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

  /**
   * @brief  实现命令自动补全，匹配目录或文件名
   *         Implements command auto-completion by matching directories or file names
   */
  void AutoComplete()
  {
    /* skip space */
    char *path = &input_line_[0];
    while (*path == ' ')
    {
      path++;
    }

    /* find last '/' in first argument */
    char *tmp = path, *path_end = path;

    while (*tmp != ' ' && *tmp != '\0')
    {
      if (*tmp == '/')
      {
        path_end = tmp;
      }
      tmp++;
    }

    /* return if not need complete */
    if (tmp - &input_line_[0] != static_cast<int>(input_line_.Size() + offset_))
    {
      return;
    }

    /* get start of prefix */
    char *prefix_start = nullptr;
    RamFS::Dir *dir = nullptr;

    if (path_end == path)
    {
      dir = current_dir_;
      prefix_start = path_end;
    }
    else
    {
      prefix_start = path_end + 1;
    }

    /* find dir*/
    if (dir == nullptr)
    {
      *path_end = '\0';
      dir = Path2Dir(path);
      *path_end = '/';
      if (dir == nullptr)
      {
        return;
      }
    }

    /* prepre for match */
    RBTree<const char *>::Node<RamFS::FsNode> *ans_node = nullptr;
    uint32_t number = 0;
    size_t same_char_number = 0;

    if (*prefix_start == '/')
    {
      prefix_start++;
    }

    int prefix_len = static_cast<int>(tmp - prefix_start);

    auto foreach_fun_find = [&](RBTree<const char *>::Node<RamFS::FsNode> &node)
    {
      if (strncmp(node->name, prefix_start, prefix_len) == 0)
      {
        ans_node = &node;
        number++;
      }

      return ErrorCode::OK;
    };

    /* start match */
    (*dir)->rbt.Foreach<RamFS::FsNode>(foreach_fun_find);

    if (number == 0)
    {
      return;
    }
    else if (number == 1)
    {
      auto name_len = strlen(ans_node->data_.name);
      for (size_t i = 0; i < name_len - prefix_len; i++)
      {
        DisplayChar(ans_node->data_.name[i + prefix_len]);
      }
    }
    else
    {
      ans_node = nullptr;
      LineFeed();

      auto foreach_fun_show = [&](RBTree<const char *>::Node<RamFS::FsNode> &node)
      {
        if (strncmp(node->name, prefix_start, prefix_len) == 0)
        {
          auto name_len = strlen(node->name);
          (*this->write_)(ConstRawData(node->name, name_len), this->write_op_);
          this->LineFeed();
          if (ans_node == nullptr)
          {
            ans_node = &node;
            same_char_number = name_len;
            return ErrorCode::OK;
          }

          for (size_t i = 0; i < name_len; i++)
          {
            if (node->name[i] != ans_node->data_.name[i])
            {
              same_char_number = i;
              break;
            }
          }

          if (same_char_number > name_len)
          {
            name_len = same_char_number;
          }

          ans_node = &node;
        }

        return ErrorCode::OK;
      };

      (*dir)->rbt.Foreach<RamFS::FsNode>(foreach_fun_show);

      ShowHeader();
      (*write_)(ConstRawData(&input_line_[0], input_line_.Size()), write_op_);

      for (size_t i = 0; i < same_char_number - prefix_len; i++)
      {
        DisplayChar(ans_node->data_.name[i + prefix_len]);
      }
    }
  }

  /**
   * @brief  处理控制字符，包括换行、删除、制表符等
   *         Handles control characters such as newline, delete, and tab
   * @param  data 输入的控制字符 The input control character
   */
  void HandleControlCharacter(char data)
  {
    if (data != '\r' && data != '\n')
    {
      linefeed_flag_ = false;
    }

    switch (data)
    {
      case '\n':
      case '\r':
        if (linefeed_flag_)
        {
          linefeed_flag_ = false;
          return;
        }
        if (history_index_ >= 0)
        {
          CopyHistoryToInputLine();
        }
        LineFeed();
        if (input_line_.Size() > 0)
        {
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

  /**
   * @brief  处理输入字符，根据类型调用相应的处理函数
   *         Handles input characters, dispatching them to the appropriate handler
   * @param  data 输入的字符 The input character
   */
  void HandleCharacter(char data)
  {
    if (flag_ansi_)
    {
      HandleAnsiCharacter(data);
    }
    else if (isprint(data))
    {
      DisplayChar(data);
    }
    else
    {
      HandleControlCharacter(data);
    }
  }

  /**
   * @brief  终端线程函数，以独立线程方式持续驱动终端
   *         Terminal thread function, continuously drives the terminal as an independent
   * thread
   *
   * @details
   * 该函数用于以独立线程的方式驱动终端，持续从输入流读取数据并解析，适用于高实时性应用。
   * 它会在循环中不断检查输入流的大小，并在数据可用时进行解析。
   *
   * This function runs as a separate thread to continuously drive the terminal,
   * reading and parsing input data in a loop. It is suitable for high real-time
   * applications. It continuously checks the input stream size and processes data when
   * available.
   *
   * @param  term 指向 Terminal 实例的指针 Pointer to the Terminal instance
   */
  static void ThreadFun(Terminal *term)
  {
    RawData buff = term->read_buff_;
    buff.size_ = LibXR::min(LibXR::max(1u, term->read_->Size()), READ_BUFF_SIZE);

    Semaphore sem;
    ReadOperation op(sem);

    while (true)
    {
      buff.size_ = LibXR::min(LibXR::max(1u, term->read_->Size()), READ_BUFF_SIZE);
      if ((*term->read_)(buff, op) == ErrorCode::OK)
      {
        buff.size_ = term->read_->read_size_;
        term->Parse(buff);
      }
    }
  }

  /**
   * @brief  终端任务函数，以定时器任务方式驱动终端
   *         Terminal task function, drives the terminal using a scheduled task
   *
   * @details
   * 该函数用于以定时任务（或轮询方式）驱动终端，适用于资源受限的系统。
   * 它不会持续运行，而是在定时器触发或系统任务调度时运行，执行一次数据读取和解析后返回。
   *
   * This function drives the terminal using a scheduled task (or polling mode),
   * making it suitable for resource-constrained systems.
   * Unlike the thread-based approach, it only runs when scheduled
   * (e.g., triggered by a timer) and processes available input data before returning.
   *
   * @param  term 指向 Terminal 实例的指针 Pointer to the Terminal instance
   */
  static void TaskFun(Terminal *term)
  {
    RawData buff = term->read_buff_;
    buff.size_ = LibXR::min(LibXR::max(1u, term->read_->Size()), READ_BUFF_SIZE);

    ReadOperation op(term->read_status_);

    while (true)
    {
      switch (term->read_status_)
      {
        case ReadOperation::OperationPollingStatus::READY:
          buff.size_ = LibXR::min(LibXR::max(1u, term->read_->Size()), READ_BUFF_SIZE);
          (*term->read_)(buff, op);
          continue;
        case ReadOperation::OperationPollingStatus::RUNNING:
          return;
        case ReadOperation::OperationPollingStatus::DONE:
          buff.size_ = term->read_->read_size_;
          term->Parse(buff);
          buff.size_ = LibXR::min(LibXR::max(1u, term->read_->Size()), READ_BUFF_SIZE);
          (*term->read_)(buff, op);
          return;
      }
    }
  }
};
}  // namespace LibXR
