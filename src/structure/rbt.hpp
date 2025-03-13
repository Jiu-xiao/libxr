#pragma once

#include <cstring>

#include "libxr_assert.hpp"
#include "libxr_def.hpp"
#include "mutex.hpp"

namespace LibXR {
template <typename Key>
class RBTree {
 public:
  enum class RbtColor : uint8_t { RED, BLACK };

  class BaseNode {
   public:
    Key key;
    RbtColor color;
    BaseNode *left = nullptr;
    BaseNode *right = nullptr;
    BaseNode *parent = nullptr;
    size_t size;

   protected:
    explicit BaseNode(size_t size) : size(size) {}
  };

  template <typename Data>
  class Node : public BaseNode {
   public:
    Node() : BaseNode(sizeof(Data)), data_{} {}
    explicit Node(const Data &data) : BaseNode(sizeof(Data)), data_(data) {}
    template <typename... Args>
    explicit Node(Args... args) : BaseNode(sizeof(Data)), data_{args...} {}

    operator Data &() { return data_; }
    Node &operator=(const Data &data) {
      data_ = data;
      return *this;
    }
    Data *operator->() { return &data_; }
    const Data *operator->() const { return &data_; }
    Data &operator*() { return data_; }

    Data data_;
  };

  explicit RBTree(int (*compare_fun)(const Key &, const Key &))
      : compare_fun_(compare_fun) {
    ASSERT(compare_fun_);
  }

  template <typename Data, SizeLimitMode LimitMode = SizeLimitMode::MORE>
  Node<Data> *Search(const Key &key) {
    mutex_.Lock();
    Node<Data> *result = nullptr;
    if (root_) {
      if (BaseNode *found = Search(root_, key)) {
        result = ToDerivedType<Data, LimitMode>(found);
      }
    }
    mutex_.Unlock();
    return result;
  }

  void Delete(BaseNode &node) {
    mutex_.Lock();

    BaseNode *child = nullptr, *parent = nullptr;
    RbtColor color = RbtColor::BLACK;

    if (node.left && node.right) {
      BaseNode *replace = node.right;
      while (replace->left) {
        replace = replace->left;
      }

      if (node.parent) {
        (node.parent->left == &node ? node.parent->left : node.parent->right) =
            replace;
      } else {
        root_ = replace;
      }

      child = replace->right;
      parent = replace->parent;
      color = replace->color;

      if (parent == &node) {
        parent = replace;
      } else {
        if (child) {
          child->parent = parent;
        }

        if (parent) {
          parent->left = child;
        }

        if (node.right) {
          replace->right = node.right;
          node.right->parent = replace;
        }
      }

      replace->parent = node.parent;
      replace->color = node.color;
      replace->left = node.left;
      node.left->parent = replace;

      if (color == RbtColor::BLACK) {
        RbtreeDeleteFixup(child, parent);
      }
      mutex_.Unlock();
      return;
    }

    child = node.left ? node.left : node.right;
    parent = node.parent;
    color = node.color;

    if (child) {
      child->parent = parent;
    }

    if (parent) {
      (parent->left == &node ? parent->left : parent->right) = child;
    } else {
      root_ = child;
    }

    if (color == RbtColor::BLACK) {
      RbtreeDeleteFixup(child, parent);
    }
    mutex_.Unlock();
  }

  template <typename KeyType>
  void Insert(BaseNode &node, KeyType &&key) {
    mutex_.Lock();
    node.left = nullptr;
    node.right = nullptr;
    node.parent = nullptr;
    node.color = RbtColor::RED;
    node.key = key;
    node.color = RbtColor::RED;
    node.key = std::forward<KeyType>(key);
    RbtreeInsert(node);
    mutex_.Unlock();
  }

  uint32_t GetNum() {
    mutex_.Lock();
    uint32_t count = 0;
    RbtreeGetNum(root_, &count);
    mutex_.Unlock();
    return count;
  }

  template <typename Data, typename Func,
            SizeLimitMode LimitMode = SizeLimitMode::MORE>
  ErrorCode Foreach(Func func) {
    mutex_.Lock();
    ErrorCode result = RbtreeForeachStart<Data>(root_, func);
    mutex_.Unlock();
    return result;
  }

  template <typename Data>
  Node<Data> *ForeachDisc(Node<Data> *node) {
    mutex_.Lock();
    Node<Data> *result = nullptr;
    if (!node) {
      result = static_cast<Node<Data> *>(root_);
      while (result && result->left) {
        result = static_cast<Node<Data> *>(result->left);
      }
    } else if (node->right) {
      result = static_cast<Node<Data> *>(node->right);
      while (result && result->left) {
        result = static_cast<Node<Data> *>(result->left);
      }
    } else if (node->parent) {
      if (node == node->parent->left) {
        result = static_cast<Node<Data> *>(node->parent);
      } else {
        while (node->parent && node == node->parent->right) {
          node = static_cast<Node<Data> *>(node->parent);
        }
        result = static_cast<Node<Data> *>(node->parent);
      }
    }
    mutex_.Unlock();
    return result;
  }

 private:
  BaseNode *root_ = nullptr;
  LibXR::Mutex mutex_;
  int (*compare_fun_)(const Key &, const Key &);

  void RbtreeInsert(BaseNode &node) {
    BaseNode *parent = nullptr;
    BaseNode **current = &root_;
    while (*current) {
      parent = *current;
      current = (compare_fun_(node.key, parent->key) < 0) ? &parent->left
                                                          : &parent->right;
    }
    node.parent = parent;
    *current = &node;
    RbtreeInsertFixup(&node);
  }

  void RbtreeInsertFixup(BaseNode *node) {
    BaseNode *parent = nullptr, *gparent = nullptr;

    while ((parent = node->parent) && parent->color == RbtColor::RED) {
      gparent = parent->parent;

      if (parent == gparent->left) {
        BaseNode *uncle = gparent->right;
        if (uncle && uncle->color == RbtColor::RED) {
          uncle->color = RbtColor::BLACK;
          parent->color = RbtColor::BLACK;
          gparent->color = RbtColor::RED;
          node = gparent;
          continue;
        }

        if (node == parent->right) {
          BaseNode *tmp = nullptr;
          RbtreeLeftRotate(parent);
          tmp = parent;
          parent = node;
          node = tmp;
        }

        parent->color = RbtColor::BLACK;
        gparent->color = RbtColor::RED;
        RbtreeRightRotate(gparent);
      } else {
        BaseNode *uncle = gparent->left;
        if (uncle && uncle->color == RbtColor::RED) {
          uncle->color = RbtColor::BLACK;
          parent->color = RbtColor::BLACK;
          gparent->color = RbtColor::RED;
          node = gparent;
          continue;
        }

        if (node == parent->left) {
          BaseNode *tmp = nullptr;
          RbtreeRightRotate(parent);
          tmp = parent;
          parent = node;
          node = tmp;
        }

        parent->color = RbtColor::BLACK;
        gparent->color = RbtColor::RED;
        RbtreeLeftRotate(gparent);
      }
    }
    root_->color = RbtColor::BLACK;
  }

  void RbtreeLeftRotate(BaseNode *x) {
    if (!x || !x->right) {
      return;
    }

    BaseNode *y = x->right;
    x->right = y->left;
    if (y->left) {
      y->left->parent = x;
    }

    y->parent = x->parent;

    if (!x->parent) {
      root_ = y;
    } else {
      if (x == x->parent->left) {
        x->parent->left = y;
      } else {
        x->parent->right = y;
      }
    }

    y->left = x;
    x->parent = y;
  }

  void RbtreeRightRotate(BaseNode *y) {
    if (!y || !y->left) {
      return;
    }

    BaseNode *x = y->left;
    y->left = x->right;
    if (x->right) {
      x->right->parent = y;
    }

    x->parent = y->parent;

    if (!y->parent) {
      root_ = x;
    } else {
      if (y == y->parent->right) {
        y->parent->right = x;
      } else {
        y->parent->left = x;
      }
    }

    x->right = y;
    y->parent = x;
  }

  void RbtreeDeleteFixup(BaseNode *node, BaseNode *parent) {
    BaseNode *other = nullptr;

    while ((!node || node->color == RbtColor::BLACK) && node != root_) {
      if (parent->left == node) {
        other = parent->right;
        if (other->color == RbtColor::RED) {
          other->color = RbtColor::BLACK;
          parent->color = RbtColor::RED;
          RbtreeLeftRotate(parent);
          other = parent->right;
        }
        if ((!other->left || other->left->color == RbtColor::BLACK) &&
            (!other->right || other->right->color == RbtColor::BLACK)) {
          other->color = RbtColor::RED;
          node = parent;
          parent = node->parent;
        } else {
          if (!other->right || other->right->color == RbtColor::BLACK) {
            other->left->color = RbtColor::BLACK;
            other->color = RbtColor::RED;
            RbtreeRightRotate(other);
            other = parent->right;
          }
          other->color = parent->color;
          parent->color = RbtColor::BLACK;
          other->right->color = RbtColor::BLACK;
          RbtreeLeftRotate(parent);
          node = root_;
          break;
        }
      } else {
        other = parent->left;
        if (other->color == RbtColor::RED) {
          other->color = RbtColor::BLACK;
          parent->color = RbtColor::RED;
          RbtreeRightRotate(parent);
          other = parent->left;
        }
        if ((!other->left || other->left->color == RbtColor::BLACK) &&
            (!other->right || other->right->color == RbtColor::BLACK)) {
          other->color = RbtColor::RED;
          node = parent;
          parent = node->parent;
        } else {
          if (!other->left || other->left->color == RbtColor::BLACK) {
            other->right->color = RbtColor::BLACK;
            other->color = RbtColor::RED;
            RbtreeLeftRotate(other);
            other = parent->left;
          }
          other->color = parent->color;
          parent->color = RbtColor::BLACK;
          other->left->color = RbtColor::BLACK;
          RbtreeRightRotate(parent);
          node = root_;
          break;
        }
      }
    }
    if (node) {
      node->color = RbtColor::BLACK;
    }
  }

  template <typename Data, typename Func>
  ErrorCode RbtreeForeachStart(BaseNode *node, Func func) {
    if (!node) {
      return ErrorCode::OK;
    }

    if (ErrorCode code = RbtreeForeach<Data, Func>(
            reinterpret_cast<Node<Data> *>(node->left), func);
        code != ErrorCode::OK) {
      return code;
    }

    if (ErrorCode code = func(*reinterpret_cast<Node<Data> *>(node));
        code != ErrorCode::OK) {
      return code;
    }

    return RbtreeForeach<Data, Func>(
        reinterpret_cast<Node<Data> *>(node->right), func);
  }

  template <typename Data, typename Func>
  ErrorCode RbtreeForeach(BaseNode *node, Func func) {
    if (!node) {
      return ErrorCode::OK;
    }

    if (ErrorCode code = RbtreeForeach<Data, Func>(
            reinterpret_cast<Node<Data> *>(node->left), func);
        code != ErrorCode::OK) {
      return code;
    }

    if (ErrorCode code = func(*reinterpret_cast<Node<Data> *>(node));
        code != ErrorCode::OK) {
      return code;
    }

    return RbtreeForeach<Data, Func>(
        reinterpret_cast<Node<Data> *>(node->right), func);
  }

  void RbtreeGetNum(BaseNode *node, uint32_t *count) {
    if (!node) {
      return;
    }
    ++(*count);
    RbtreeGetNum(node->left, count);
    RbtreeGetNum(node->right, count);
  }

  BaseNode *Search(BaseNode *x, const Key &key) {
    while (x) {
      int cmp = compare_fun_(key, x->key);
      if (cmp == 0) {
        break;
      }
      x = cmp < 0 ? x->left : x->right;
    }
    return x;
  }

  template <typename Data, SizeLimitMode LimitMode>
  static Node<Data> *ToDerivedType(BaseNode *node) {
    if (node) {
      Assert::SizeLimitCheck<LimitMode>(sizeof(Data), node->size);
    }
    return static_cast<Node<Data> *>(node);
  }
};
}  // namespace LibXR
