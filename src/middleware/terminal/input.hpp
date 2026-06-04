  /**
   * @brief  解析输入数据流，将其转换为字符并处理
   *         Parses the input data stream, converting it into characters and processing
   * them
   * @param  raw_data 输入的原始数据 Input raw data
   */
  void Parse(RawData& raw_data)
  {
    char* buff = static_cast<char*>(raw_data.addr_);
    for (size_t i = 0; i < raw_data.size_; i++)
    {
      HandleCharacter(buff[i]);
    }
  }

  /**
   * @brief  处理 ANSI 序列中的后续字符
   *         Handles the follow-up characters of an ANSI sequence
   * @param  data 输入字符 The input character
   */
  void HandleAnsiCharacter(char data)
  {
    if (flag_ansi_ == 1)
    {
      if (std::isprint(static_cast<unsigned char>(data)))
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
            write_stream_ << ConstRawData(KEY_RIGHT, sizeof(KEY_RIGHT) - 1);
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
            write_stream_ << ConstRawData(KEY_LEFT, sizeof(KEY_LEFT) - 1);
          }
          break;
        default:
          break;
      }

      flag_ansi_ = 0;
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
      linefeed_char_ = '\0';
    }

    switch (data)
    {
      case '\n':
      case '\r':
        if (linefeed_flag_ && data != linefeed_char_)
        {
          linefeed_flag_ = false;
          linefeed_char_ = '\0';
          return;
        }
        linefeed_flag_ = true;
        linefeed_char_ = data;
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
      case '\b':
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
    else if (std::isprint(static_cast<unsigned char>(data)))
    {
      DisplayChar(data);
    }
    else
    {
      HandleControlCharacter(data);
    }
  }
