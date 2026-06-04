#include "ramfs.hpp"

using namespace LibXR;

RamFS::RamFS(const char* name) : root_(name), bin_("bin") { root_.Add(bin_); }

int RamFS::CompareStr(const char* const& a, const char* const& b) { return strcmp(a, b); }

char* RamFS::DuplicateName(const char* name)
{
  ASSERT(name != nullptr);
  if (name == nullptr)
  {
    return nullptr;
  }

  char* name_buff = new char[strlen(name) + 1];
  strcpy(name_buff, name);
  return name_buff;
}

RamFS::FsNode::FsNode(FsNodeType node_type) : type_(node_type), tree_node_(this) {}

RamFS::FsNode::FsNode(const FsNode& other)
    : name_(other.name_), type_(other.type_), parent_(nullptr), tree_node_(this)
{
}

RamFS::File::File() : FsNode(FsNodeType::FILE), addr_(nullptr), arg_(nullptr), size_(0) {}

RamFS::File::File(const char* name) : File() { this->name_ = DuplicateName(name); }

int RamFS::File::Run(int argc, char** argv)
{
  ASSERT(file_type_ == FileType::EXEC);
  ASSERT(exec_ != nullptr);
  return exec_(arg_, argc, argv);
}

RamFS::Custom::Custom() : FsNode(FsNodeType::CUSTOM) {}

RamFS::Custom::Custom(const char* name, uint32_t kind, void* context) : Custom()
{
  this->name_ = DuplicateName(name);
  this->kind_ = kind;
  this->context_ = context;
}

RamFS::Dir::Dir() : FsNode(FsNodeType::DIR), rbt_(RamFS::CompareStr) {}

RamFS::Dir::Dir(const char* name) : Dir() { this->name_ = DuplicateName(name); }

void RamFS::Dir::AddNode(FsNode& node)
{
  ASSERT(node.name_ != nullptr);
  ASSERT(FindNode(node.name_) == nullptr);
  node.parent_ = this;
  rbt_.Insert(node.tree_node_, node.name_);
}

RamFS::FsNode* RamFS::Dir::FindNode(const char* name)
{
  if (name == nullptr)
  {
    return nullptr;
  }

  auto* node = rbt_.Search<FsNode*>(name);
  return node != nullptr ? node->data_ : nullptr;
}

RamFS::FsNode* RamFS::Dir::FindNodeByType(const char* name, FsNodeType type)
{
  auto* ans = FindNode(name);
  if (ans == nullptr || ans->GetNodeType() != type)
  {
    return nullptr;
  }
  return ans;
}

RamFS::FsNode* RamFS::Dir::FindNodeRevByType(const char* name, FsNodeType type)
{
  auto* ans = FindNodeByType(name, type);
  if (ans != nullptr)
  {
    return ans;
  }

  struct FindNodeRevFn
  {
    const char* name_;
    FsNodeType type_;
    FsNode*& ans_;

    ErrorCode operator()(Tree::Node<FsNode*>& item)
    {
      auto* node = item.data_;
      if (node->GetNodeType() != FsNodeType::DIR)
      {
        return ErrorCode::OK;
      }

      ans_ = static_cast<Dir*>(node)->FindNodeRevByType(name_, type_);
      return ans_ != nullptr ? ErrorCode::FAILED : ErrorCode::OK;
    }
  };

  FindNodeRevFn find{name, type, ans};
  rbt_.Foreach<FsNode*>(find);
  return ans;
}

RamFS::File* RamFS::Dir::FindFile(const char* name)
{
  return static_cast<File*>(FindNodeByType(name, FsNodeType::FILE));
}

RamFS::File* RamFS::Dir::FindFileRev(const char* name)
{
  return static_cast<File*>(FindNodeRevByType(name, FsNodeType::FILE));
}

RamFS::Dir* RamFS::Dir::FindDir(const char* name)
{
  if (name == nullptr)
  {
    return nullptr;
  }

  if (name[0] == '.' && name[1] == '\0')
  {
    return this;
  }

  if (name[0] == '.' && name[1] == '.' && name[2] == '\0')
  {
    return parent_;
  }

  return static_cast<Dir*>(FindNodeByType(name, FsNodeType::DIR));
}

RamFS::Dir* RamFS::Dir::FindDirRev(const char* name)
{
  if (name == nullptr)
  {
    return nullptr;
  }

  if (name[0] == '.' && name[1] == '\0')
  {
    return this;
  }

  if (name[0] == '.' && name[1] == '.' && name[2] == '\0')
  {
    return parent_;
  }

  return static_cast<Dir*>(FindNodeRevByType(name, FsNodeType::DIR));
}

RamFS::Custom* RamFS::Dir::FindCustom(const char* name)
{
  return static_cast<Custom*>(FindNodeByType(name, FsNodeType::CUSTOM));
}

RamFS::Custom* RamFS::Dir::FindCustomRev(const char* name)
{
  return static_cast<Custom*>(FindNodeRevByType(name, FsNodeType::CUSTOM));
}
