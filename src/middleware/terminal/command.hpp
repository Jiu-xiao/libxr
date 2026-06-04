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
  RamFS::Dir* Path2Dir(char* path)
  {
    if (path == nullptr)
    {
      return nullptr;
    }

    size_t index = 0;
    RamFS::Dir* dir = current_dir_;

    if (*path == '/')
    {
      index++;
      dir = &ramfs_.root_;
      if (path[index] == '\0')
      {
        return dir;
      }
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
        index = static_cast<size_t>(tmp - path + 1);
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
  RamFS::File* Path2File(char* path)
  {
    if (path == nullptr)
    {
      return nullptr;
    }

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
    RamFS::Dir* dir = name == path ? &ramfs_.root_ : Path2Dir(path);
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
      RamFS::Dir* dir = arg_number_ >= 2 ? Path2Dir(arg_tab_[1]) : nullptr;
      if (dir != nullptr)
      {
        current_dir_ = dir;
      }
      LineFeed();
      return;
    }

    if (strcmp(arg_tab_[0], "ls") == 0)
    {
      auto ls_fun = [&](RamFS::FsNode& item)
      {
        switch (item.GetNodeType())
        {
          case RamFS::FsNodeType::DIR:
            write_stream_ << ConstRawData("d ");
            break;
          case RamFS::FsNodeType::FILE:
            if (static_cast<RamFS::File&>(item).IsExecutable())
            {
              write_stream_ << ConstRawData("x ");
            }
            else
            {
              write_stream_ << ConstRawData("f ");
            }
            break;
          case RamFS::FsNodeType::CUSTOM:
            write_stream_ << ConstRawData("? ");
            break;
          default:
            write_stream_ << ConstRawData("? ");
            break;
        }
        write_stream_ << ConstRawData(item.GetName());
        this->LineFeed();
        return ErrorCode::OK;
      };

      current_dir_->Foreach(ls_fun);
      return;
    }

    auto* ans = Path2File(arg_tab_[0]);

    if (ans == nullptr)
    {
      write_stream_ << ConstRawData("Command not found.");
      LineFeed();
      return;
    }

    if (!ans->IsExecutable())
    {
      write_stream_ << ConstRawData("Not an executable file.");
      LineFeed();
      return;
    }

    write_stream_.Commit();
    write_mutex_->Unlock();
    ans->Run(arg_number_, arg_tab_);
    write_mutex_->Lock();
  }

  /**
   * @brief  实现命令自动补全，匹配目录或文件名
   *         Implements command auto-completion by matching directories or file names
   */
  void AutoComplete()
  {
    /* skip space */
    char* path = &input_line_[0];
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
    char* prefix_start = nullptr;
    RamFS::Dir* dir = nullptr;

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
    RamFS::FsNode* ans_node = nullptr;
    uint32_t number = 0;
    size_t shared_prefix_len = 0;

    if (*prefix_start == '/')
    {
      prefix_start++;
    }

    int prefix_len = static_cast<int>(tmp - prefix_start);

    auto foreach_fun_find = [&](RamFS::FsNode& node)
    {
      if (strncmp(node.GetName(), prefix_start, prefix_len) == 0)
      {
        ans_node = &node;
        number++;
      }

      return ErrorCode::OK;
    };

    /* start match */
    dir->Foreach(foreach_fun_find);

    if (number == 0)
    {
      return;
    }
    else if (number == 1)
    {
      auto name_len = strlen(ans_node->GetName());
      for (size_t i = 0; i < name_len - prefix_len; i++)
      {
        DisplayChar(ans_node->GetName()[i + prefix_len]);
      }
    }
    else
    {
      ans_node = nullptr;
      LineFeed();

      auto foreach_fun_show = [&](RamFS::FsNode& node)
      {
        if (strncmp(node.GetName(), prefix_start, prefix_len) == 0)
        {
          auto name_len = strlen(node.GetName());
          write_stream_ << ConstRawData(node.GetName(), name_len);
          this->LineFeed();
          if (ans_node == nullptr)
          {
            ans_node = &node;
            shared_prefix_len = name_len;
            return ErrorCode::OK;
          }

          for (size_t i = 0; i < name_len; i++)
          {
            if (node.GetName()[i] != ans_node->GetName()[i])
            {
              shared_prefix_len = i;
              break;
            }
          }

          // Once one candidate is shorter than the previously retained shared
          // prefix, the shared prefix must shrink to this candidate length.
          if (shared_prefix_len > name_len)
          {
            shared_prefix_len = name_len;
          }

          ans_node = &node;
        }

        return ErrorCode::OK;
      };

      dir->Foreach(foreach_fun_show);

      ShowHeader();
      write_stream_ << ConstRawData(&input_line_[0], input_line_.Size());

      for (size_t i = 0; i < shared_prefix_len - prefix_len; i++)
      {
        DisplayChar(ans_node->GetName()[i + prefix_len]);
      }
    }
  }
