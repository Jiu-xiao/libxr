#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_ramfs()
{
  auto ramfs = LibXR::RamFS();

  int ramfs_arg = 0;

  auto file = LibXR::RamFS::CreateFile<int*>(
      "test_file",
      [](int* arg, int argc, char** argv)
      {
        UNUSED(argc);
        UNUSED(argv);
        *arg = *arg + 1;
        return 0;
      },
      &ramfs_arg);

  auto file_1 = LibXR::RamFS::CreateFile("test_file1", ramfs_arg);

  auto dir = LibXR::RamFS::CreateDir("test_dir");

  auto dev = LibXR::RamFS::Device("test_dev");
  file_1->size = 4;
  for (int i = 1; i < 10; i++)
  {
    file->Run(0, nullptr);
    ASSERT(file_1->GetData<int>() == i);
  }

  ramfs.Add(dir);
  ramfs.Add(file_1);
  dir.Add(file);
  dir.Add(dev);

  ASSERT(ramfs.FindDir("test") == nullptr);
  ASSERT(ramfs.FindFile("test") == nullptr);
  ASSERT(ramfs.FindDevice("test") == nullptr);
  ASSERT(dir.FindDevice("test") == nullptr);
  ASSERT(dir.FindFile("test") == nullptr);

  ASSERT(ramfs.FindDir("test_dir") == &dir);
  ASSERT(ramfs.FindFile("test_file") == &file);
  ASSERT(ramfs.FindDevice("test_dev") == &dev);
  ASSERT(dir.FindDevice("test_dev") == &dev);
  ASSERT(dir.FindFile("test_file") == &file);
}
