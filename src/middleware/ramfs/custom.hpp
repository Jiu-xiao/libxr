  /**
   * @brief `RamFS` 的自定义节点片段 / Custom-node fragment of `RamFS`
   */
  /**
   * @class Custom
   * @brief 用户自定义节点，RamFS 仅负责命名和查找 / User-defined node; RamFS only
   * stores and finds it by name
   */
  class Custom : public FsNode
  {
   public:
    /**
     * @brief 构造自定义节点 / Construct a custom node
     * @param name 节点名称 / Node name
     * @param kind 用户定义类型 / User-defined kind
     * @param context 用户上下文指针 / User context pointer
     *
     * @note 包含动态内存分配 / Contains dynamic memory allocation
     */
    explicit Custom(const char* name, uint32_t kind = 0, void* context = nullptr);

    uint32_t kind_ = 0;        ///< 用户定义类型标签 / User-defined kind tag.
    void* context_ = nullptr;  ///< 用户自管上下文指针 / Caller-owned user context pointer.

   private:
    /**
     * @brief 构造一个空自定义节点壳 / Construct one empty custom-node shell
     */
    Custom();
  };
