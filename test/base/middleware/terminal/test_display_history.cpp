/**
 * @file test_display_history.cpp
 * @brief `Terminal` 历史显示与行内重绘场景子测试。 Split test unit for `Terminal` history display and inline-redraw scenarios.
 * @details 测试项目：
 *          1. 历史显示与 `CopyHistoryToInputLine()` 恢复。
 *          2. 光标偏移下的插入/删除重绘后缀。
 *          Test items:
 *          1. History display and `CopyHistoryToInputLine()` restoration.
 *          2. Insert/delete redraw suffixes with a non-zero cursor offset.
 */
#include "terminal_display_test_common.hpp"

namespace
{

/**
 * @brief 测试项函数 `TestHistoryDisplayAndRestore`。 Test-item function `TestHistoryDisplayAndRestore`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestHistoryDisplayAndRestore()
{
  // 测试内容：验证历史回显、索引推进和复制回输入行后的保留状态。
  // Test coverage: verify history rendering, index movement, and retained state after copying back to the input line.
  TerminalDisplayFixture<LibXR::Terminal<>::Mode::CRLF> fixture;

  FillInputLine(fixture.terminal, "alpha");
  fixture.terminal.AddHistory();
  FillInputLine(fixture.terminal, "beta");
  fixture.terminal.AddHistory();

  fixture.terminal.history_index_ = 0;
  fixture.terminal.offset_ = -3;
  fixture.terminal.ShowHistory();
  ASSERT(fixture.terminal.offset_ == 0);
  ASSERT(fixture.FlushOutput() == "\033[2K\rramfs:/$ beta");

  fixture.terminal.history_index_ = 1;
  fixture.terminal.ShowHistory();
  ASSERT(fixture.FlushOutput() == "\033[2K\rramfs:/$ alpha");

  fixture.terminal.CopyHistoryToInputLine();
  ASSERT(fixture.terminal.history_index_ == -1);
  ASSERT(fixture.terminal.offset_ == 0);
  ASSERT(fixture.terminal.input_line_.Size() == 5);
  ASSERT(std::strcmp(&fixture.terminal.input_line_[0], "alpha") == 0);
}

/**
 * @brief 测试项函数 `TestMidLineDisplayEditing`。 Test-item function `TestMidLineDisplayEditing`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestMidLineDisplayEditing()
{
  // 测试内容：验证有光标偏移时的插入/删除如何重绘尾部内容。
  // Test coverage: verify how insertion and deletion redraw the trailing text when the cursor is offset from the end.
  TerminalDisplayFixture<LibXR::Terminal<>::Mode::CRLF> fixture;

  FillInputLine(fixture.terminal, "ab");
  fixture.terminal.offset_ = -1;
  fixture.terminal.DisplayChar('X');
  ASSERT(std::strcmp(&fixture.terminal.input_line_[0], "aXb") == 0);
  ASSERT(fixture.FlushOutput() == "X\033[s\033[Kb\033[u");

  fixture.terminal.DeleteChar();
  ASSERT(std::strcmp(&fixture.terminal.input_line_[0], "ab") == 0);
  ASSERT(fixture.FlushOutput() == "\b \b\033[s\033[Kb\033[u");
}

}  // namespace

/**
 * @brief 测试项函数 `RunTerminalDisplayHistoryTests`。 Test-item function `RunTerminalDisplayHistoryTests`.
 * @details 测试内容：执行 `Terminal` 历史显示与行内重绘子场景。 Execute `Terminal` history-display and inline-redraw sub-scenarios.
 *          测试原理：把历史与行内重绘语义单独成组，集中覆盖保留状态和可见输出的一致性。 Group history and inline-redraw semantics around the consistency between retained state and visible output.
 */
void RunTerminalDisplayHistoryTests()
{
  TestHistoryDisplayAndRestore();
  TestMidLineDisplayEditing();
}
