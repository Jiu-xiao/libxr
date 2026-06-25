/**
 * @file test_input_edit.cpp
 * @brief `Terminal` 光标移动后插入编辑场景子测试。 Split test unit for `Terminal` mid-line edit-after-cursor-move scenarios.
 * @details 测试项目：
 *          1. 左移后插入字符会在命令中间生效。
 *          2. 行内编辑后的最终命令会按期望名称执行。
 *          Test items:
 *          1. Inserting after moving left edits the command in the middle.
 *          2. The final command name after inline editing executes as expected.
 */
#include "terminal_session_test_common.hpp"

namespace
{

/**
 * @brief 测试项函数 `TestMidLineInputEditing`。 Test-item function `TestMidLineInputEditing`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestMidLineInputEditing()
{
  // 测试内容：验证左移后继续输入时，命令缓冲区和最终执行名称都会按中间插入语义更新。
  // Test coverage: verify that after moving left, continued typing updates both the command buffer and the final executed name as a mid-line insertion.
  TerminalFixture fixture;

  int acbd_count = 0;
  CommandState acbd_state{"acbd", &acbd_count};
  auto acbd_cmd =
      LibXR::RamFS::CreateCommand<CommandState*>("acbd", CountCommand, &acbd_state);
  fixture.ramfs.Add(acbd_cmd);

  constexpr char KEY_LEFT_LEFT[] = "\033[D\033[D";

  fixture.SendText("abd");
  auto cursor_moves = fixture.SendRaw(KEY_LEFT_LEFT, sizeof(KEY_LEFT_LEFT) - 1);
  ASSERT(CountSubstring(cursor_moves, "\033[D") == 2);
  fixture.SendText("c\n");
  ASSERT(acbd_count == 1);
}

}  // namespace

/**
 * @brief 测试项函数 `RunTerminalInputEditTests`。 Test-item function `RunTerminalInputEditTests`.
 * @details 测试内容：执行 `Terminal` 行内编辑子场景。 Execute `Terminal` inline-edit sub-scenarios.
 *          测试原理：把行内编辑单独成组，聚焦光标移动后的输入缓冲区更新契约。 Group inline editing around the buffer-update contract after cursor movement.
 */
void RunTerminalInputEditTests()
{
  TestMidLineInputEditing();
}
