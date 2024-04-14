#include "libxr_assert.hpp"
#include "libxr_def.hpp"
#include "mutex.hpp"

namespace LibXR {
template <typename Data> class RBTree {
public:
  enum RBTColor { RBT_COLOR_RED, RBT_COLOR_BLACK };

  class Node {
  public:
    Data key;
    RBTColor color;
    Node *left;
    Node *right;
    Node *parent;

    const Data &operator=(const Data &data) {
      key = data;
      return key;
    }
  };

  RBTree(int (*compare_fun_)(const Data &, const Data &))
      : root_(NULL), compare_fun_(compare_fun_) {
    ASSERT(compare_fun_);
  }

  Node &Search(const Data &key) {
    mutex_.Lock();
    auto ans = _Search(root_, key);
    mutex_.UnLock();
    return ans;
  }

  void Delete(Node &node) {
    mutex_.Lock();

    Node *child, *parent;
    RBTColor color;

    if ((node.left != NULL) && (node.right != NULL)) {
      Node *replace = &node;

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

  bool Insert(Node &node) {
    node.left = NULL;
    node.right = NULL;
    node.parent = NULL;
    node.color = RBT_COLOR_BLACK;

    mutex_.Lock();
    RbtreeInsert(node);
    mutex_.UnLock();

    return true;
  }

  uint32_t GetNum() {
    uint32_t num = 0;
    mutex_.Lock();
    _RbtreeGetNum(root_, &num);
    mutex_.UnLock();
    return num;
  }

  template <typename ArgType>
  ErrorCode Foreach(ErrorCode (*fun)(Node &node, ArgType arg), ArgType arg) {
    mutex_.Lock();
    auto ans = RbtreeForeach(root_, fun, arg);
    mutex_.UnLock();
    return ans;
  }

  Node *ForeachDisc(Node *node) {
    mutex_.Lock();
    if (node == NULL) {
      node = root_;
      while (node->left != NULL) {
        node = node->left;
      }
      mutex_.UnLock();
      return node;
    }

    if (node->right != NULL) {
      node = node->right;
      while (node->left != NULL) {
        node = node->left;
      }
      mutex_.UnLock();
      return node;
    }

    if (node->parent != NULL) {
      if (node == node->parent->left) {
        mutex_.UnLock();
        return node->parent;
      } else {
        while (node->parent != NULL && node == node->parent->right) {
          node = node->parent;
        }
        mutex_.UnLock();
        return node->parent;
      }
    }

    mutex_.UnLock();
    return NULL;
  }

private:
  Node *GetParent(Node *node) { return node->parent; }

  RBTColor GetColor(Node *node) { return node->color; }

  bool IsRed(Node *node) { return node->color == RBT_COLOR_RED; }

  bool IsBlack(Node *node) { return node->color == RBT_COLOR_BLACK; }

  void SetBlack(Node *node) { node->color = RBT_COLOR_BLACK; }

  void SetRed(Node *node) { node->color = RBT_COLOR_RED; }

  void SetParent(Node *node, Node *parent) { node->parent = parent; }

  void SetColor(Node *node, RBTColor color) { node->color = color; }

  Node *_Search(Node *x, const Data &key) {
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

  void RbtreeDeleteFixup(Node *node, Node *parent) {
    Node *other;

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

  void RbtreeInsert(Node &node) {
    Node *y = NULL;
    Node *x = root_;

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

  void _RbtreeGetNum(Node *node, uint32_t *num) {
    if (node == NULL)
      return;

    (*num)++;

    _RbtreeGetNum(node->left, num);
    _RbtreeGetNum(node->right, num);
  }

  void RbtreeInsertFixup(Node *node) {
    Node *parent, *gparent;

    while ((parent = GetParent(node)) && IsRed(parent)) {
      gparent = GetParent(parent);

      if (parent == gparent->left) {
        {
          Node *uncle = gparent->right;
          if (uncle && IsRed(uncle)) {
            SetBlack(uncle);
            SetBlack(parent);
            SetRed(gparent);
            node = gparent;
            continue;
          }
        }

        if (parent->right == node) {
          Node *tmp;
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
          Node *uncle = gparent->left;
          if (uncle && IsRed(uncle)) {
            SetBlack(uncle);
            SetBlack(parent);
            SetRed(gparent);
            node = gparent;
            continue;
          }
        }

        if (parent->left == node) {
          Node *tmp;
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

  void RbtreeLeftRotate(Node *x) {
    Node *y = x->right;

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

  void RbtreeRightRotate(Node *y) {
    Node *x = y->left;

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

  template <typename ArgType>
  ErrorCode RbtreeForeach(Node *node, ErrorCode (*fun)(Node &node, ArgType arg),
                          ArgType arg) {
    if (node == NULL) {
      return NO_ERR;
    }

    if (RbtreeForeach(node->left, fun, arg) == NO_ERR &&
        fun(*node, arg) == NO_ERR) {
      return RbtreeForeach(node->right, fun, arg);
    }

    return NO_ERR;
  }

  Node *root_;

  LibXR::Mutex mutex_;

  int (*compare_fun_)(const Data &, const Data &);
};
} // namespace LibXR