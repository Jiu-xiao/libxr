#pragma once

#include "libxr_assert.hpp"
#include "libxr_def.hpp"
#include "mutex.hpp"
#include <cstring>

namespace LibXR {
template <typename Key> class RBTree {
public:
  enum RBTColor { RBT_COLOR_RED, RBT_COLOR_BLACK };

  class BaseNode {
  public:
    Key key;
    RBTColor color;
    BaseNode *left = NULL;
    BaseNode *right = NULL;
    BaseNode *parent = NULL;
    size_t size;

  protected:
    BaseNode(size_t size) : size(size) {}
  };

  template <typename Data> class Node : public BaseNode {
  public:
    Node() : BaseNode(sizeof(Data)) {}
    Node(const Data &data) : BaseNode(sizeof(Data)), data_(data) {}

    operator Data &() { return data_; }

    const Data &operator=(const Data &data) {
      data_ = data;
      return data_;
    }

    Data data_;
  };

  RBTree(int (*compare_fun_)(const Key &, const Key &))
      : root_(NULL), compare_fun_(compare_fun_) {
    ASSERT(compare_fun_);
  }

  template <typename Data> Node<Data> *Search(const Key &key) {
    mutex_.Lock();
    auto ans = _Search(root_, key);
    mutex_.UnLock();
    return ToDerivedType<Data>(ans);
  }

  void Delete(BaseNode &node) {
    mutex_.Lock();

    BaseNode *child, *parent;
    RBTColor color;

    if ((node.left != NULL) && (node.right != NULL)) {
      BaseNode *replace = &node;

      replace = replace->right;
      while (replace->left != NULL)
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

      if (color == RBT_COLOR_BLACK)
        RbtreeDeleteFixup(child, parent);
      mutex_.UnLock();
      return;
    }

    if (node.left != NULL)
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

    if (color == RBT_COLOR_BLACK)
      RbtreeDeleteFixup(child, parent);

    mutex_.UnLock();
  }

  void Insert(BaseNode &node, Key &&key) {
    node.left = NULL;
    node.right = NULL;
    node.parent = NULL;
    node.color = RBT_COLOR_BLACK;
    node.key = key;

    mutex_.Lock();
    RbtreeInsert(node);
    mutex_.UnLock();
  }

  void Insert(BaseNode &node, Key &key) {
    node.left = NULL;
    node.right = NULL;
    node.parent = NULL;
    node.color = RBT_COLOR_BLACK;
    node.key = key;

    mutex_.Lock();
    RbtreeInsert(node);
    mutex_.UnLock();
  }

  uint32_t GetNum() {
    uint32_t num = 0;
    mutex_.Lock();
    _RbtreeGetNum(root_, &num);
    mutex_.UnLock();
    return num;
  }

  template <typename Data, typename ArgType>
  ErrorCode Foreach(ErrorCode (*fun)(Node<Data> &node, ArgType arg),
                    ArgType arg) {

    typedef struct {
      ErrorCode (*fun_)(Node<Data> &node, ArgType arg);
      ArgType arg_;
    } Block;

    Block block = {.fun_ = fun, .arg_ = arg};

    ErrorCode (*foreach_fun)(BaseNode & node, void *arg) = [](BaseNode &node,
                                                              void *raw) {
      Block *block = reinterpret_cast<Block *>(raw);
      return block->fun_(ToDerivedType<Data>(node), block->arg_);
    };

    mutex_.Lock();
    auto ans = RbtreeForeach(root_, foreach_fun, &block);
    mutex_.UnLock();
    return ans;
  }

  template <typename Data> Node<Data> *ForeachDisc(Node<Data> *node) {
    mutex_.Lock();
    if (node == NULL) {
      node = ToDerivedType<Data>(root_);
      while (node->left != NULL) {
        node = ToDerivedType<Data>(node->left);
      }
      mutex_.UnLock();
      return node;
    }

    if (node->right != NULL) {
      node = ToDerivedType<Data>(node->right);
      while (node->left != NULL) {
        node = ToDerivedType<Data>(node->left);
      }
      mutex_.UnLock();
      return node;
    }

    if (node->parent != NULL) {
      if (node == node->parent->left) {
        mutex_.UnLock();
        return ToDerivedType<Data>(node->parent);
      } else {
        while (node->parent != NULL && node == node->parent->right) {
          node = ToDerivedType<Data>(node->parent);
        }
        mutex_.UnLock();
        return ToDerivedType<Data>(node->parent);
      }
    }

    mutex_.UnLock();
    return NULL;
  }

private:
  BaseNode *GetParent(BaseNode *node) { return node->parent; }

  RBTColor GetColor(BaseNode *node) { return node->color; }

  bool IsRed(BaseNode *node) { return node->color == RBT_COLOR_RED; }

  bool IsBlack(BaseNode *node) { return node->color == RBT_COLOR_BLACK; }

  void SetBlack(BaseNode *node) { node->color = RBT_COLOR_BLACK; }

  void SetRed(BaseNode *node) { node->color = RBT_COLOR_RED; }

  void SetParent(BaseNode *node, BaseNode *parent) { node->parent = parent; }

  void SetColor(BaseNode *node, RBTColor color) { node->color = color; }

  template <typename Data> static Node<Data> *ToDerivedType(BaseNode *node) {
    if (node) {
      ASSERT(node->size == sizeof(Data));
    }
    return reinterpret_cast<Node<Data> *>(node);
  }

  template <typename Data> static Node<Data> &ToDerivedType(BaseNode &node) {
    if (&node != NULL) {
      ASSERT(node.size == sizeof(Data));
    }
    return *reinterpret_cast<Node<Data> *>(&node);
  }
  BaseNode *_Search(BaseNode *x, const Key &key) {
    if (x == NULL)
      return NULL;

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
    BaseNode *y = NULL;
    BaseNode *x = root_;

    while (x != NULL) {
      y = x;
      if (compare_fun_(node.key, x->key) < 0)
        x = x->left;
      else
        x = x->right;
    }
    node.parent = y;

    if (y != NULL) {
      if (compare_fun_(node.key, y->key) < 0)
        y->left = &node;
      else
        y->right = &node;
    } else {
      root_ = &node;
    }

    node.color = RBT_COLOR_RED;

    RbtreeInsertFixup(&node);
  }

  void _RbtreeGetNum(BaseNode *node, uint32_t *num) {
    if (node == NULL)
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
    if (y->left != NULL)
      y->left->parent = x;

    y->parent = x->parent;

    if (x->parent == NULL) {
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
    if (x->right != NULL)
      x->right->parent = y;

    x->parent = y->parent;

    if (y->parent == NULL) {
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
    if (node == NULL) {
      return NO_ERR;
    }

    if (RbtreeForeach(node->left, fun, arg) == NO_ERR &&
        fun(*node, arg) == NO_ERR) {
      return RbtreeForeach(node->right, fun, arg);
    }

    return NO_ERR;
  }

  BaseNode *root_;

  LibXR::Mutex mutex_;

  int (*compare_fun_)(const Key &, const Key &);
};
} // namespace LibXR