#pragma once

#include "libxr_assert.hpp"
#include "libxr_def.hpp"
#include "mutex.hpp"
#include <cstring>

namespace LibXR {
template <typename Key> class RBTree {
public:
  enum class RBTColor { RED, BLACK };

  class BaseNode {
  public:
    Key key;
    RBTColor color;
    BaseNode *left = nullptr;
    BaseNode *right = nullptr;
    BaseNode *parent = nullptr;
    size_t size;

  protected:
    BaseNode(size_t size) : size(size) {}
  };

  template <typename Data> class Node : public BaseNode {
  public:
    Node() : BaseNode(sizeof(Data)), data_() {}
    Node(const Data &data) : BaseNode(sizeof(Data)), data_(data) {}

    operator Data &() { return data_; }

    const Data &operator=(const Data &data) {
      data_ = data;
      return data_;
    }

    Data &GetData() { return data_; }

    Data data_;
  };

  RBTree(int (*compare_fun_)(const Key &, const Key &))
      : root_(nullptr), compare_fun_(compare_fun_) {
    ASSERT(compare_fun_);
  }

  template <typename Data>
  Node<Data> *Search(const Key &key,
                     SizeLimitMode limit_mode = SizeLimitMode::MORE) {
    mutex_.Lock();
    if (root_ == nullptr) {
      mutex_.Unlock();
      return nullptr;
    }
    auto ans = _Search(root_, key);
    mutex_.Unlock();
    return ToDerivedType<Data>(ans, limit_mode);
  }

  void Delete(BaseNode &node) {
    mutex_.Lock();

    BaseNode *child, *parent;
    RBTColor color;

    if ((node.left != nullptr) && (node.right != nullptr)) {
      BaseNode *replace = &node;

      replace = replace->right;
      while (replace->left != nullptr)
        replace = replace->left;

      if (GetParent(&node)) {
        if (GetParent(&node)->left == &node)
          GetParent(&node)->left = replace;
        else
          GetParent(&node)->right = replace;
      } else
        root_ = replace;

      child = replace->right;
      parent = GetParent(replace);
      color = GetColor(replace);

      if (parent == &node) {
        parent = replace;
      } else {
        if (child)
          SetParent(child, parent);
        parent->left = child;

        replace->right = node.right;
        SetParent(node.right, replace);
      }

      replace->parent = node.parent;
      replace->color = node.color;
      replace->left = node.left;
      node.left->parent = replace;

      if (color == RBTColor::BLACK)
        RbtreeDeleteFixup(child, parent);
      mutex_.Unlock();
      return;
    }

    if (node.left != nullptr)
      child = node.left;
    else
      child = node.right;

    parent = node.parent;
    color = node.color;

    if (child)
      child->parent = parent;

    if (parent) {
      if (parent->left == &node)
        parent->left = child;
      else
        parent->right = child;
    } else
      root_ = child;

    if (color == RBTColor::BLACK)
      RbtreeDeleteFixup(child, parent);

    mutex_.Unlock();
  }

  void Insert(BaseNode &node, Key &&key) {
    node.left = nullptr;
    node.right = nullptr;
    node.parent = nullptr;
    node.color = RBTColor::BLACK;
    node.key = key;

    mutex_.Lock();
    RbtreeInsert(node);
    mutex_.Unlock();
  }

  void Insert(BaseNode &node, Key &key) {
    node.left = nullptr;
    node.right = nullptr;
    node.parent = nullptr;
    node.color = RBTColor::BLACK;
    node.key = key;

    mutex_.Lock();
    RbtreeInsert(node);
    mutex_.Unlock();
  }

  uint32_t GetNum() {
    uint32_t num = 0;
    mutex_.Lock();
    _RbtreeGetNum(root_, &num);
    mutex_.Unlock();
    return num;
  }

  template <typename Data, typename ArgType>
  ErrorCode Foreach(ErrorCode (*fun)(Node<Data> &node, ArgType arg),
                    ArgType arg,
                    SizeLimitMode limit_mode = SizeLimitMode::MORE) {

    typedef struct {
      ErrorCode (*fun_)(Node<Data> &node, ArgType arg);
      ArgType arg_;
      SizeLimitMode limit_mode_;
    } Block;

    Block block;
    block.fun_ = fun;
    block.arg_ = arg;
    block.limit_mode_ = limit_mode;

    auto foreach_fun = [](BaseNode &node, void *raw) {
      Block *block = reinterpret_cast<Block *>(raw);
      Assert::SizeLimitCheck(sizeof(Data), node.size, block->limit_mode_);
      return block->fun_(*ToDerivedType<Data>(&node), block->arg_);
    };

    mutex_.Lock();
    auto ans = RbtreeForeach(root_, foreach_fun, &block);
    mutex_.Unlock();
    return ans;
  }

  template <typename Data> Node<Data> *ForeachDisc(Node<Data> *node) {
    mutex_.Lock();
    if (node == nullptr) {
      node = ToDerivedType<Data>(root_);
      while (node->left != nullptr) {
        node = ToDerivedType<Data>(node->left);
      }
      mutex_.Unlock();
      return node;
    }

    if (node->right != nullptr) {
      node = ToDerivedType<Data>(node->right);
      while (node->left != nullptr) {
        node = ToDerivedType<Data>(node->left);
      }
      mutex_.Unlock();
      return node;
    }

    if (node->parent != nullptr) {
      if (node == node->parent->left) {
        mutex_.Unlock();
        return ToDerivedType<Data>(node->parent);
      } else {
        while (node->parent != nullptr && node == node->parent->right) {
          node = ToDerivedType<Data>(node->parent);
        }
        mutex_.Unlock();
        return ToDerivedType<Data>(node->parent);
      }
    }

    mutex_.Unlock();
    return nullptr;
  }

private:
  BaseNode *GetParent(BaseNode *node) { return node->parent; }

  RBTColor GetColor(BaseNode *node) { return node->color; }

  bool IsRed(BaseNode *node) { return node->color == RBTColor::RED; }

  bool IsBlack(BaseNode *node) { return node->color == RBTColor::BLACK; }

  void SetBlack(BaseNode *node) { node->color = RBTColor::BLACK; }

  void SetRed(BaseNode *node) { node->color = RBTColor::RED; }

  void SetParent(BaseNode *node, BaseNode *parent) { node->parent = parent; }

  void SetColor(BaseNode *node, RBTColor color) { node->color = color; }

  template <typename Data>
  static Node<Data> *
  ToDerivedType(BaseNode *node,
                SizeLimitMode limit_mode = SizeLimitMode::MORE) {
    if (node) {
      Assert::SizeLimitCheck(sizeof(Data), node->size, limit_mode);
    }
    return reinterpret_cast<Node<Data> *>(node);
  }

  BaseNode *_Search(BaseNode *x, const Key &key) {
    if (x == nullptr) {
      return nullptr;
    }

    int ans = compare_fun_(key, x->key);

    if (ans == 0)
      return x;
    else if (ans < 0)
      return _Search(x->left, key);
    else
      return _Search(x->right, key);
  }

  void RbtreeDeleteFixup(BaseNode *node, BaseNode *parent) {
    BaseNode *other;

    while ((!node || IsBlack(node)) && node != root_) {
      if (parent->left == node) {
        other = parent->right;
        if (IsRed(other)) {
          SetBlack(other);
          SetRed(parent);
          RbtreeLeftRotate(parent);
          other = parent->right;
        }
        if ((!other->left || IsBlack(other->left)) &&
            (!other->right || IsBlack(other->right))) {
          SetRed(other);
          node = parent;
          parent = GetParent(node);
        } else {
          if (!other->right || IsBlack(other->right)) {
            SetBlack(other->left);
            SetRed(other);
            RbtreeRightRotate(other);
            other = parent->right;
          }
          SetColor(other, GetColor(parent));
          SetBlack(parent);
          SetBlack(other->right);
          RbtreeLeftRotate(parent);
          node = root_;
          break;
        }
      } else {
        other = parent->left;
        if (IsRed(other)) {
          SetBlack(other);
          SetRed(parent);
          RbtreeRightRotate(parent);
          other = parent->left;
        }
        if ((!other->left || IsBlack(other->left)) &&
            (!other->right || IsBlack(other->right))) {
          SetRed(other);
          node = parent;
          parent = GetParent(node);
        } else {
          if (!other->left || IsBlack(other->left)) {
            SetBlack(other->right);
            SetRed(other);
            RbtreeLeftRotate(other);
            other = parent->left;
          }
          SetColor(other, GetColor(parent));
          SetBlack(parent);
          SetBlack(other->left);
          RbtreeRightRotate(parent);
          node = root_;
          break;
        }
      }
    }
    if (node)
      SetBlack(node);
  }

  void RbtreeInsert(BaseNode &node) {
    BaseNode *y = nullptr;
    BaseNode *x = root_;

    while (x != nullptr) {
      y = x;
      if (compare_fun_(node.key, x->key) < 0)
        x = x->left;
      else
        x = x->right;
    }
    node.parent = y;

    if (y != nullptr) {
      if (compare_fun_(node.key, y->key) < 0)
        y->left = &node;
      else
        y->right = &node;
    } else {
      root_ = &node;
    }

    node.color = RBTColor::RED;

    RbtreeInsertFixup(&node);
  }

  void _RbtreeGetNum(BaseNode *node, uint32_t *num) {
    if (node == nullptr)
      return;

    (*num)++;

    _RbtreeGetNum(node->left, num);
    _RbtreeGetNum(node->right, num);
  }

  void RbtreeInsertFixup(BaseNode *node) {
    BaseNode *parent, *gparent;

    while ((parent = GetParent(node)) && IsRed(parent)) {
      gparent = GetParent(parent);

      if (parent == gparent->left) {
        {
          BaseNode *uncle = gparent->right;
          if (uncle && IsRed(uncle)) {
            SetBlack(uncle);
            SetBlack(parent);
            SetRed(gparent);
            node = gparent;
            continue;
          }
        }

        if (parent->right == node) {
          BaseNode *tmp;
          RbtreeLeftRotate(parent);
          tmp = parent;
          parent = node;
          node = tmp;
        }

        SetBlack(parent);
        SetRed(gparent);
        RbtreeRightRotate(gparent);
      } else {
        {
          BaseNode *uncle = gparent->left;
          if (uncle && IsRed(uncle)) {
            SetBlack(uncle);
            SetBlack(parent);
            SetRed(gparent);
            node = gparent;
            continue;
          }
        }

        if (parent->left == node) {
          BaseNode *tmp;
          RbtreeRightRotate(parent);
          tmp = parent;
          parent = node;
          node = tmp;
        }

        SetBlack(parent);
        SetRed(gparent);
        RbtreeLeftRotate(gparent);
      }
    }

    SetBlack(root_);
  }

  void RbtreeLeftRotate(BaseNode *x) {
    BaseNode *y = x->right;

    x->right = y->left;
    if (y->left != nullptr)
      y->left->parent = x;

    y->parent = x->parent;

    if (x->parent == nullptr) {
      root_ = y;
    } else {
      if (x->parent->left == x)
        x->parent->left = y;
      else
        x->parent->right = y;
    }

    y->left = x;
    x->parent = y;
  }

  void RbtreeRightRotate(BaseNode *y) {
    BaseNode *x = y->left;

    y->left = x->right;
    if (x->right != nullptr)
      x->right->parent = y;

    x->parent = y->parent;

    if (y->parent == nullptr) {
      root_ = x;
    } else {
      if (y == y->parent->right)
        y->parent->right = x;
      else
        y->parent->left = x;
    }

    x->right = y;
    y->parent = x;
  }

  ErrorCode RbtreeForeach(BaseNode *node,
                          ErrorCode (*fun)(BaseNode &node, void *arg),
                          void *arg) {
    if (node == nullptr) {
      return ErrorCode::OK;
    }

    if (RbtreeForeach(node->left, fun, arg) == ErrorCode::OK &&
        fun(*node, arg) == ErrorCode::OK) {
      return RbtreeForeach(node->right, fun, arg);
    }

    return ErrorCode::OK;
  }

  BaseNode *root_;

  LibXR::Mutex mutex_;

  int (*compare_fun_)(const Key &, const Key &);
};
} // namespace LibXR