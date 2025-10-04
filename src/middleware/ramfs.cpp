#include "ramfs.hpp"

using namespace LibXR;

RamFS::RamFS(const char *name)
    : root_(CreateDir(name)), bin_(CreateDir("bin")), dev_(CreateDir("dev"))
{
  root_.Add(bin_);
  root_.Add(dev_);
}

int RamFS::CompareStr(const char *const &a, const char *const &b) { return strcmp(a, b); }

int RamFS::FileNode::Run(int argc, char **argv)
{
  ASSERT(type == FileType::EXEC);
  return exec(arg, argc, argv);
}

RamFS::Device::Device(const char *name, const ReadPort &read_port,
                      const WritePort &write_port)
{
  char *name_buff = new char[strlen(name) + 1];
  strcpy(name_buff, name);
  data_.name = name_buff;
  data_.type = FsNodeType::DEVICE;

  UNUSED(read_port);
  UNUSED(write_port);
}

RamFS::File *RamFS::Dir::FindFile(const char *name)
{
  auto ans = (*this)->rbt.Search<FsNode>(name);
  if (ans && ans->data_.type == FsNodeType::FILE)
  {
    return reinterpret_cast<File *>(ans);
  }
  else
  {
    return nullptr;
  }
}

RamFS::File *RamFS::Dir::FindFileRev(const char *name)
{
  auto ans = FindFile(name);
  if (ans)
  {
    return ans;
  }

  struct FindFileRevFn
  {
    const char *name;
    RamFS::File *&ans;
    ErrorCode operator()(RBTree<const char *>::Node<RamFS::FsNode> &item) const
    {
      RamFS::FsNode &node = item;
      if (node.type == FsNodeType::DIR)
      {
        auto *dir = reinterpret_cast<RamFS::Dir *>(&item);

        auto f = dir->FindFile(name);
        if (f)
        {
          ans = f;
          return ErrorCode::FAILED;
        }

        dir->data_.rbt.Foreach<RamFS::FsNode>(FindFileRevFn{name, ans});
        return ans ? ErrorCode::FAILED : ErrorCode::OK;
      }
      return ErrorCode::OK;
    }
  };

  data_.rbt.Foreach<FsNode>(FindFileRevFn{name, ans});
  return ans;
}

RamFS::Dir *RamFS::Dir::FindDir(const char *name)
{
  if (name[0] == '.' && name[1] == '\0')
  {
    return this;
  }

  if (name[0] == '.' && name[1] == '.' && name[2] == '\0')
  {
    return reinterpret_cast<Dir *>(data_.parent);
  }

  auto ans = (*this)->rbt.Search<RamFS::FsNode>(name);

  if (ans && (*ans)->type == FsNodeType::DIR)
  {
    return reinterpret_cast<Dir *>(ans);
  }
  else
  {
    return nullptr;
  }
}

RamFS::Dir *RamFS::Dir::FindDirRev(const char *name)
{
  auto ans = FindDir(name);
  if (ans)
  {
    return ans;
  }

  struct FindDirRevFn
  {
    const char *name;
    RamFS::Dir *&ans;
    ErrorCode operator()(RBTree<const char *>::Node<RamFS::FsNode> &item) const
    {
      RamFS::FsNode &node = item;
      if (node.type == FsNodeType::DIR)
      {
        auto *dir = reinterpret_cast<RamFS::Dir *>(&item);
        if (strcmp(dir->data_.name, name) == 0)
        {
          ans = dir;
          return ErrorCode::FAILED;
        }

        dir->data_.rbt.Foreach<RamFS::FsNode>(FindDirRevFn{name, ans});
        return ans ? ErrorCode::FAILED : ErrorCode::OK;
      }
      return ErrorCode::OK;
    }
  };

  data_.rbt.Foreach<FsNode>(FindDirRevFn{name, ans});
  return ans;
}

RamFS::Device *RamFS::Dir::FindDeviceRev(const char *name)
{
  auto ans = FindDevice(name);
  if (ans)
  {
    return ans;
  }

  struct FindDevRevFn
  {
    const char *name;
    RamFS::Device *&ans;
    ErrorCode operator()(RBTree<const char *>::Node<RamFS::FsNode> &item) const
    {
      RamFS::FsNode &node = item;
      if (node.type == FsNodeType::DIR)
      {
        auto *dir = reinterpret_cast<RamFS::Dir *>(&item);

        auto d = dir->FindDevice(name);
        if (d)
        {
          ans = d;
          return ErrorCode::FAILED;
        }

        dir->data_.rbt.Foreach<RamFS::FsNode>(FindDevRevFn{name, ans});
        return ans ? ErrorCode::FAILED : ErrorCode::OK;
      }
      return ErrorCode::OK;
    }
  };

  data_.rbt.Foreach<FsNode>(FindDevRevFn{name, ans});
  return ans;
}

/**
 * @brief  在当前目录中查找设备
 *         Finds a device in the current directory
 * @param  name 设备名 The name of the device
 * @return Device* 指向设备的指针，如果未找到则返回 nullptr
 *         Pointer to the device, returns nullptr if not found
 */
RamFS::Device *RamFS::Dir::FindDevice(const char *name)
{
  auto ans = (*this)->rbt.Search<FsNode>(name);
  if (ans && ans->data_.type == FsNodeType::DEVICE)
  {
    return reinterpret_cast<Device *>(ans);
  }
  else
  {
    return nullptr;
  }
}
