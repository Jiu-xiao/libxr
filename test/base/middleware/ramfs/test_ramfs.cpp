/**
 * @file test_ramfs.cpp
 * @brief `RamFS` 节点创建、层级查找与类型化文件访问测试。 `RamFS` node creation,
 * hierarchy and typed-file access tests.
 *
 * 测试项目 / Test items:
 * 1. 可执行文件行为。 Executable file behavior: verify executable files run, mutate bound
 * state and stay distinguishable from ordinary files.
 * 2. 可写/只读文件的数据访问视图。 Read/write and read-only data views: verify typed
 * access, const access and raw view sizing for writable and read-only files.
 * 3. 目录、文件、自定义节点的层级查找与遍历。 Hierarchy lookup: verify recursive
 * file/dir/custom lookup, `.` / `..` directory traversal and direct-child enumeration
 * counts.
 *
 * 测试原理 / Test principles:
 * 1. 构造一个混合节点树，让查找和遍历都跑在真实运行时结构上。 Construct a real mixed node
 * tree containing file, dir, exec and custom nodes so lookup and traversal execute on the
 * same structure users build at runtime.
 * 2. 同时检查数据访问和结构查询，因为 RamFS 契约覆盖存储和命名空间两部分。 Observe both
 * typed data access and structural queries, because RamFS correctness spans storage
 * semantics and namespace semantics together.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

/**
 * @brief 测试入口函数 `test_ramfs`。 Test entry function `test_ramfs`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
 */
void test_ramfs()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  auto ramfs = LibXR::RamFS();

  int ramfs_arg = 0;

  auto command = LibXR::RamFS::CreateFile<int*>(
      "test_command",
      [](int* arg, int argc, char** argv)
      {
        UNUSED(argc);
        UNUSED(argv);
        *arg = *arg + 1;
        return 0;
      },
      &ramfs_arg);

  auto file_1 = LibXR::RamFS::CreateFile("test_file1", ramfs_arg);
  TEST_ASSERT(command.IsExecutable());
  TEST_ASSERT(!file_1.IsExecutable());

  auto dir = LibXR::RamFS::CreateDir("test_dir");
  auto nested_dir = LibXR::RamFS::CreateDir("nested_dir");

  for (int i = 1; i < 10; i++)
  {
    command.Run(0, nullptr);
    TEST_ASSERT(file_1.Data<const int>() == i);
  }

  file_1.Data<int>() = 42;
  TEST_ASSERT(ramfs_arg == 42);

  const uint32_t ro_value = 0x12345678;
  auto ro_file = LibXR::RamFS::CreateFile("ro_file", ro_value);
  TEST_ASSERT(ro_file.IsReadOnly());
  TEST_ASSERT(ro_file.Data<const uint32_t>() == ro_value);
  const auto& ro_file_view = ro_file;
  TEST_ASSERT(ro_file_view.Data().size_ == sizeof(ro_value));

  uint16_t nested_value = 0x55AA;
  auto nested_file = LibXR::RamFS::CreateFile("nested_file", nested_value);

  uint32_t custom_context = 0xA5A55A5A;
  auto custom = LibXR::RamFS::Custom("custom_node", 0x42, &custom_context);

  ramfs.Add(dir);
  ramfs.Add(file_1);
  dir.Add(command);
  dir.Add(ro_file);
  dir.Add(nested_dir);
  dir.Add(custom);
  nested_dir.Add(nested_file);

  TEST_ASSERT(ramfs.FindDir("test") == nullptr);
  TEST_ASSERT(ramfs.FindFile("test") == nullptr);
  TEST_ASSERT(ramfs.FindCustom("test") == nullptr);
  TEST_ASSERT(dir.FindFile("test") == nullptr);

  TEST_ASSERT(ramfs.FindDir("test_dir") == &dir);
  TEST_ASSERT(ramfs.FindFile("test_file1") == &file_1);
  TEST_ASSERT(ramfs.FindFile("test_command") == &command);
  TEST_ASSERT(ramfs.FindFile("nested_file") == &nested_file);
  TEST_ASSERT(ramfs.FindCustom("custom_node") == &custom);
  TEST_ASSERT(dir.FindFile("test_command") == &command);
  TEST_ASSERT(dir.FindCustom("custom_node") == &custom);
  TEST_ASSERT(dir.FindNode("custom_node") == &custom);
  TEST_ASSERT(custom.kind_ == 0x42);
  TEST_ASSERT(custom.context_ == &custom_context);
  TEST_ASSERT(dir.FindFile("ro_file") == &ro_file);
  TEST_ASSERT(dir.FindDir(".") == &dir);
  TEST_ASSERT(dir.FindDir("..") == &ramfs.root_);
  TEST_ASSERT(nested_dir.FindDir("..") == &dir);

  uint32_t direct_child_count = 0;
  uint32_t direct_file_count = 0;
  uint32_t direct_dir_count = 0;
  uint32_t direct_custom_count = 0;
  dir.Foreach(
      [&](LibXR::RamFS::FsNode& node)
      {
        direct_child_count++;
        switch (node.GetNodeType())
        {
          case LibXR::RamFS::FsNodeType::FILE:
            direct_file_count++;
            break;
          case LibXR::RamFS::FsNodeType::DIR:
            direct_dir_count++;
            break;
          case LibXR::RamFS::FsNodeType::CUSTOM:
            direct_custom_count++;
            break;
          default:
            TEST_ASSERT(false);
            break;
        }
        return LibXR::ErrorCode::OK;
      });
  TEST_ASSERT(direct_child_count == 4);
  TEST_ASSERT(direct_file_count == 2);
  TEST_ASSERT(direct_dir_count == 1);
  TEST_ASSERT(direct_custom_count == 1);
}
