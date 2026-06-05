  /**
   * @brief `RamFS` 的公共节点基类片段 / Common node-base fragment of `RamFS`
   */
  /**
   * @class FsNode
   * @brief 文件系统节点基类 / Base class for all RamFS nodes
   */
  class FsNode
  {
   public:
    /**
     * @brief 获取节点类型 / Get the node type
     * @return 节点类型 / Node type
     */
    [[nodiscard]] FsNodeType GetNodeType() const { return type_; }

    /**
     * @brief 获取节点名称 / Get the node name
     * @return 节点名称 / Node name
     */
    [[nodiscard]] const char* GetName() const { return name_; }

   protected:
    const char* name_ = nullptr;  ///< 节点名称缓冲区 / Retained node-name buffer.
    FsNodeType type_;             ///< 节点运行时类型 / Runtime node type.
    Dir* parent_ = nullptr;       ///< 父目录；根目录保持为空 / Parent directory; stays null for the root.

    /**
     * @brief 用指定节点类型构造基类部分 / Construct the base node with a given node type
     * @param node_type 节点类型 / Node type
     */
    explicit FsNode(FsNodeType node_type);

    /**
     * @brief 拷贝构造节点基类部分 / Copy-construct the base-node portion
     * @param other 被拷贝的节点 / Node to copy from
     *
     * @note 这里只复制节点元数据，不复制父目录关系；新对象默认重新脱离原目录树。
     *       This copies only node metadata and does not preserve parent
     *       linkage; the new object is detached from the original directory
     *       tree by default.
     */
    FsNode(const FsNode& other);
    FsNode& operator=(const FsNode&) = delete;

    Tree::Node<FsNode*> tree_node_;  ///< 当前节点挂进目录树时使用的树节点包装 / Tree node wrapper used when inserted into a directory tree.

    friend class Dir;
  };
