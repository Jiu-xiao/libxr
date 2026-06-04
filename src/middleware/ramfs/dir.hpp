  /**
   * @class Dir
   * @brief 目录节点，管理直属子节点 / Directory node that owns a child namespace
   */
  class Dir : public FsNode
  {
   public:
    /**
     * @brief 添加直属文件节点 / Add a direct child file node
     * @param file 文件节点 / File node
     */
    void Add(File& file) { AddNode(file); }

    /**
     * @brief 添加直属目录节点 / Add a direct child directory node
     * @param dir 目录节点 / Directory node
     */
    void Add(Dir& dir) { AddNode(dir); }

    /**
     * @brief 添加直属自定义节点 / Add a direct child custom node
     * @param custom 自定义节点 / Custom node
     */
    void Add(Custom& custom) { AddNode(custom); }

    /**
     * @brief 查找直属子节点 / Find a direct child node
     * @param name 节点名称 / Node name
     * @return 子节点指针；未找到返回 nullptr / Child node pointer, or nullptr
     */
    FsNode* FindNode(const char* name);

    /**
     * @brief 查找直属文件 / Find a direct child file
     * @param name 文件名 / File name
     * @return 文件指针；未找到返回 nullptr / File pointer, or nullptr
     */
    File* FindFile(const char* name);

    /**
     * @brief 递归查找文件 / Find a file recursively
     * @param name 文件名 / File name
     * @return 文件指针；未找到返回 nullptr / File pointer, or nullptr
     */
    File* FindFileRev(const char* name);

    /**
     * @brief 查找直属目录，支持 "." 和 ".." / Find a direct child directory,
     * supporting "." and ".."
     * @param name 目录名 / Directory name
     * @return 目录指针；未找到返回 nullptr / Directory pointer, or nullptr
     */
    Dir* FindDir(const char* name);

    /**
     * @brief 递归查找目录，支持 "." 和 ".." / Find a directory recursively,
     * supporting "." and ".."
     * @param name 目录名 / Directory name
     * @return 目录指针；未找到返回 nullptr / Directory pointer, or nullptr
     */
    Dir* FindDirRev(const char* name);

    /**
     * @brief 查找直属自定义节点 / Find a direct child custom node
     * @param name 节点名称 / Node name
     * @return 自定义节点指针；未找到返回 nullptr / Custom node pointer, or nullptr
     */
    Custom* FindCustom(const char* name);

    /**
     * @brief 递归查找自定义节点 / Find a custom node recursively
     * @param name 节点名称 / Node name
     * @return 自定义节点指针；未找到返回 nullptr / Custom node pointer, or nullptr
     */
    Custom* FindCustomRev(const char* name);

    /**
     * @brief 遍历直属子节点 / Iterate over direct child nodes
     * @tparam Func 回调类型 / Callback type
     * @param func 回调函数，返回非 OK 时停止遍历 / Callback; non-OK stops iteration
     * @return 遍历结果 / Iteration result
     */
    template <typename Func>
    ErrorCode Foreach(Func func)
    {
      return rbt_.Foreach<FsNode*>([&](Tree::Node<FsNode*>& node)
                                   { return func(*node.data_); });
    }

   private:
    Dir();
    explicit Dir(const char* name);

    void AddNode(FsNode& node);
    FsNode* FindNodeByType(const char* name, FsNodeType type);
    FsNode* FindNodeRevByType(const char* name, FsNodeType type);

    Tree rbt_;

    friend class RamFS;
  };
