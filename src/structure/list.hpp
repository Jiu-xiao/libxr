#pragma once

#include <utility>

#include "libxr_assert.hpp"
#include "libxr_def.hpp"
#include "mutex.hpp"

namespace LibXR
{

/**
 * @brief 链表实现，用于存储和管理数据节点。
 *        A linked list implementation for storing and managing data nodes.
 *
 * 该类提供了基本的链表操作，包括添加、删除节点，以及遍历链表的功能，
 * 具有线程安全的特性。
 * This class provides fundamental linked list operations,
 * including adding, deleting nodes, and traversing the list,
 * with thread-safety features.
 */
class List
{
 public:
  /**
   * @brief 链表基础节点，所有节点都继承自该类。
   *        Base node for the linked list, serving as a parent for all nodes.
   */
  class BaseNode
  {
   public:
    /**
     * @brief 构造 `BaseNode` 并设置节点大小。
     *        Constructs a `BaseNode` and sets its size.
     *
     * @param size 节点所占用的字节数。
     *             The size of the node in bytes.
     */
    BaseNode(size_t size) : size_(size) {}

    /**
     * @brief 析构函数，确保节点不会在列表中残留。
     *        Destructor ensuring the node does not remain in the list.
     */
    ~BaseNode() { ASSERT(next_ == nullptr); }

    BaseNode* next_ = nullptr;  ///< 指向下一个节点的指针。 Pointer to the next node.
    size_t size_;  ///< 当前节点的数据大小（字节）。 Size of the current node (in bytes).
  };

  /**
   * @brief 数据节点模板，继承自 `BaseNode`，用于存储具体数据类型。
   *        Template data node that inherits from `BaseNode` to store specific data types.
   *
   * @tparam Data 存储的数据类型。
   *             The type of data stored.
   */
  template <typename Data>
  class Node : public BaseNode
  {
   public:
    /**
     * @brief 默认构造函数，初始化节点大小。
     *        Default constructor initializing the node size.
     */
    Node() : BaseNode(sizeof(Data)) {}

    /**
     * @brief 使用数据值构造 `Node` 节点。
     *        Constructs a `Node` with the given data value.
     *
     * @param data 要存储的数据。
     *             The data to be stored.
     */
    explicit Node(const Data& data) : BaseNode(sizeof(Data)), data_(data) {}

    /**
     * @brief 通过参数列表构造节点 (Constructor initializing a node using arguments list).
     * @tparam Args 参数类型 (Types of arguments for data initialization).
     * @param args 数据构造参数 (Arguments used for constructing the data).
     */
    template <typename... Args>
    explicit Node(Args... args) : BaseNode(sizeof(Data)), data_{args...}
    {
    }

    /**
     * @brief 赋值运算符重载，允许直接对节点赋值。
     *        Overloaded assignment operator for assigning values to the node.
     *
     * @param data 赋值的数据。
     *             The data to be assigned.
     * @return 返回修改后的 `Node` 对象引用。
     *         Returns a reference to the modified `Node` object.
     */
    Node& operator=(const Data& data)
    {
      data_ = data;
      return *this;
    }

    /**
     * @brief 操作符重载，提供数据访问接口。
     *        Operator overloads providing access to the data.
     */
    Data* operator->() noexcept { return &data_; }
    const Data* operator->() const noexcept { return &data_; }
    Data& operator*() noexcept { return data_; }
    operator Data&() noexcept { return data_; }

    Data data_;  ///< 存储的数据。 The stored data.
  };

  /**
   * @brief 默认构造函数，初始化链表头节点。
   *        Default constructor initializing the linked list head node.
   */
  List() noexcept : head_(0) { head_.next_ = &head_; }

  /**
   * @brief 析构函数，释放所有节点。
   *        Destructor releasing all nodes.
   */
  ~List()
  {
    for (auto pos = head_.next_; pos != &head_;)
    {
      auto tmp = pos->next_;
      pos->next_ = nullptr;
      pos = tmp;
    }

    head_.next_ = nullptr;
  }

  /**
   * @brief 向链表添加一个节点。
   *        Adds a node to the linked list.
   *
   * @param data 要添加的 `BaseNode` 节点。
   *             The `BaseNode` node to be added.
   */
  void Add(BaseNode& data)
  {
    mutex_.Lock();
    data.next_ = head_.next_;
    head_.next_ = &data;
    mutex_.Unlock();
  }

  /**
   * @brief 获取链表中的节点数量。
   *        Gets the number of nodes in the linked list.
   *
   * @return 返回链表中节点的数量。
   *         Returns the number of nodes in the list.
   */
  uint32_t Size() noexcept
  {
    uint32_t size = 0;
    mutex_.Lock();

    for (auto pos = head_.next_; pos != &head_; pos = pos->next_)
    {
      ++size;
    }

    mutex_.Unlock();
    return size;
  }

  /**
   * @brief 从链表中删除指定的节点。
   *        Deletes a specified node from the linked list.
   *
   * @param data 要删除的 `BaseNode` 节点。
   *             The `BaseNode` node to be deleted.
   * @return 返回 `ErrorCode`，指示操作是否成功。
   *         Returns `ErrorCode`, indicating whether the operation was successful.
   */
  ErrorCode Delete(BaseNode& data) noexcept
  {
    mutex_.Lock();
    for (auto pos = &head_; pos->next_ != &head_; pos = pos->next_)
    {
      if (pos->next_ == &data)
      {
        pos->next_ = data.next_;
        data.next_ = nullptr;
        mutex_.Unlock();
        return ErrorCode::OK;
      }
    }
    mutex_.Unlock();
    return ErrorCode::NOT_FOUND;
  }

  /**
   * @brief 遍历链表中的每个节点，并应用回调函数。
   *        Iterates over each node in the list and applies a callback function.
   *
   * @tparam Data 存储的数据类型。
   *             The type of stored data.
   * @tparam Func 回调函数类型。
   *              The callback function type.
   * @tparam LimitMode 大小限制模式，默认为 `MORE`。
   *                   Size limit mode, default is `MORE`.
   * @param func 需要应用于每个节点数据的回调函数。
   *             The callback function to be applied to each node's data.
   * @return 返回 `ErrorCode`，指示操作是否成功。
   *         Returns `ErrorCode`, indicating whether the operation was successful.
   */
  template <typename Data, typename Func, SizeLimitMode LimitMode = SizeLimitMode::MORE>
  ErrorCode Foreach(Func func)
  {
    mutex_.Lock();
    for (auto pos = head_.next_; pos != &head_; pos = pos->next_)
    {
      Assert::SizeLimitCheck<LimitMode>(sizeof(Data), pos->size_);
      if (auto res = func(static_cast<Node<Data>*>(pos)->data_); res != ErrorCode::OK)
      {
        mutex_.Unlock();
        return res;
      }
    }
    mutex_.Unlock();
    return ErrorCode::OK;
  }

  /**
   * @brief 在回调环境中遍历链表节点，并应用回调函数。
   *        Iterates over each node in a callback environment and applies a function.
   *
   * @tparam Data 存储的数据类型。
   *             The type of stored data.
   * @tparam Func 回调函数类型。
   *              The callback function type.
   * @tparam LimitMode 大小限制模式，默认为 `MORE`。
   *                   Size limit mode, default is `MORE`.
   * @param func 需要应用于每个节点数据的回调函数。
   *             The callback function to be applied to each node's data.
   * @param in_isr 指示当前是否在中断服务例程（ISR）中。
   *               Indicates whether the function is called in an ISR.
   * @return 返回 `ErrorCode`，指示操作是否成功。
   *         Returns `ErrorCode`, indicating whether the operation was successful.
   */
  template <typename Data, typename Func, SizeLimitMode LimitMode = SizeLimitMode::MORE>
  ErrorCode ForeachFromCallback(Func func, bool in_isr)
  {
    if (mutex_.TryLockInCallback(in_isr) != ErrorCode::OK)
    {
      return ErrorCode::BUSY;
    }

    for (auto pos = head_.next_; pos != &head_; pos = pos->next_)
    {
      Assert::SizeLimitCheck<LimitMode>(sizeof(Data), pos->size_);
      if (auto res = func(static_cast<Node<Data>*>(pos)->data_); res != ErrorCode::OK)
      {
        mutex_.UnlockFromCallback(in_isr);
        return res;
      }
    }
    mutex_.UnlockFromCallback(in_isr);
    return ErrorCode::OK;
  }

 private:
  BaseNode head_;       ///< 链表头节点。 The head node of the list.
  LibXR::Mutex mutex_;  ///< 线程安全的互斥锁。 Thread-safe mutex.
};

}  // namespace LibXR
