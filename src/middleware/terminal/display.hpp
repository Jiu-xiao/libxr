  static size_t HistoryLineSize(const HistoryLine& line)
  {
    size_t size = 0;
    while (size < MAX_LINE_SIZE && line[size] != '\0')
    {
      size++;
    }
    return size;
  }

  /**
   * @brief  按“从最新到最旧”的顺序取历史记录
   *         Fetches one history entry counting from newest to oldest
   * @param  index_from_newest 从最新一条开始的偏移；0 表示最新一条
   *         Offset counted from the newest entry; 0 means the newest one
   * @return const HistoryLine& 对应的历史记录
   *         Reference to the requested history entry
   *
   * @note `Queue::operator[]` 支持负索引从尾部反向访问，但这里把这个细节包起来，
   *       避免调用点直接暴露 `history_[-index - 1]` 这种难读写法。
   *       `Queue::operator[]` supports negative indices to walk backward from
   *       the tail, but this helper hides that detail so call sites do not have
   *       to expose the hard-to-read `history_[-index - 1]` form directly.
   */
  const HistoryLine& HistoryFromNewest(int index_from_newest)
  {
    ASSERT(index_from_newest >= 0);
    ASSERT(index_from_newest < static_cast<int>(history_.Size()));
    return history_[-index_from_newest - 1];
  }

  /**
   * @brief  执行换行操作
   *         Performs a line feed operation
   */
  void LineFeed()
  {
    if (MODE == Mode::CRLF)
    {
      write_stream_ << ConstRawData("\r\n");
    }
    else if (MODE == Mode::LF)
    {
      write_stream_ << ConstRawData('\n');
    }
    else if (MODE == Mode::CR)
    {
      write_stream_ << ConstRawData('\r');
    }
  }

  /**
   * @brief  更新光标位置
   *         Updates cursor position
   */
  void UpdateDisplayPosition()
  {
    write_stream_ << ConstRawData(KEY_SAVE) << ConstRawData(CLEAR_BEHIND)
                  << ConstRawData(&input_line_[input_line_.Size() + offset_], -offset_)
                  << ConstRawData(KEY_LOAD);
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
        write_stream_ << ConstRawData(input_line_[input_line_.Size() - 1 + offset_]);
      }
      if (offset_ != 0)
      {
        UpdateDisplayPosition();
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
        write_stream_ << ConstRawData(DELETE_CHAR);
      }

      if (offset_ != 0)
      {
        UpdateDisplayPosition();
      }
    }
  }

  /**
   * @brief  显示终端提示符，包括当前目录信息
   *         Displays the terminal prompt, including the current directory information
   */
  void ShowHeader()
  {
    write_stream_ << ConstRawData(ramfs_.root_.GetName(), strlen(ramfs_.root_.GetName()));
    if (current_dir_ == &ramfs_.root_)
    {
      write_stream_ << ConstRawData(":/");
    }
    else
    {
      write_stream_ << ConstRawData(":") << ConstRawData(current_dir_->GetName());
    }

    write_stream_ << ConstRawData("$ ");
  }

  /**
   * @brief  清除当前行
   *         Clears the current line
   */
  void ClearLine() { write_stream_ << ConstRawData(CLEAR_LINE); }

  /**
   * @brief  清除整个终端屏幕
   *         Clears the entire terminal screen
   */
  void Clear() { write_stream_ << ConstRawData(CLEAR_ALL); }

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
      const auto& line = HistoryFromNewest(history_index_);
      write_stream_ << ConstRawData(line.data(), HistoryLineSize(line));
    }
    else
    {
      write_stream_ << ConstRawData(&input_line_[0], input_line_.Size());
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
    const auto& line = HistoryFromNewest(history_index_);
    for (size_t i = 0; i < HistoryLineSize(line); i++)
    {
      input_line_.Push(line[i]);
    }
    input_line_[input_line_.Size()] = '\0';
    history_index_ = -1;
    offset_ = 0;
  }

  /**
   * @brief  将当前输入行添加到历史记录
   *         Adds the current input line to the history
   */
  void AddHistory()
  {
    HistoryLine line{};
    const size_t line_size =
        LibXR::min(static_cast<size_t>(input_line_.Size()), MAX_LINE_SIZE);
    input_line_.Push('\0');
    if (line_size > 0)
    {
      std::memcpy(line.data(), &input_line_[0], line_size);
    }
    line[line_size] = '\0';

    if (history_.EmptySize() == 0)
    {
      history_.Pop();
    }
    history_.Push(line);
  }
