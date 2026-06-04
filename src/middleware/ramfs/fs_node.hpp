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
    const char* name_ = nullptr;
    FsNodeType type_;
    Dir* parent_ = nullptr;

    explicit FsNode(FsNodeType node_type);
    FsNode(const FsNode& other);
    FsNode& operator=(const FsNode&) = delete;

    Tree::Node<FsNode*> tree_node_;

    friend class Dir;
  };
