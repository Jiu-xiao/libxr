/**
 * @file test_display_modes.cpp
 * @brief `Terminal` 换行与清屏显示场景子测试。 Split test unit for `Terminal` line-feed and clear-sequence scenarios.
 * @details 测试项目：
 *          1. `CRLF` / `LF` / `CR` 三种换行模式输出。
 *          2. prompt、清行和清屏序列输出。
 *          Test items:
 *          1. Output under `CRLF` / `LF` / `CR` line-feed modes.
 *          2. Prompt, clear-line, and clear-screen sequence output.
 */
#include "terminal_display_test_common.hpp"

namespace
{

/**
 * @brief 测试项函数 `TestLineFeedModes`。 Test-item function `TestLineFeedModes`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestLineFeedModes()
{
  // 测试内容：验证三种换行模式各自输出的终端换行字节。
  // Test coverage: verify the terminal line-ending bytes emitted by the three line-feed modes.
  TerminalDisplayFixture<LibXR::Terminal<>::Mode::CRLF> crlf_fixture;
  crlf_fixture.terminal.LineFeed();
  ASSERT(crlf_fixture.FlushOutput() == "\r\n");

  TerminalDisplayFixture<LibXR::Terminal<>::Mode::LF> lf_fixture;
  lf_fixture.terminal.LineFeed();
  ASSERT(lf_fixture.FlushOutput() == "\n");

  TerminalDisplayFixture<LibXR::Terminal<>::Mode::CR> cr_fixture;
  cr_fixture.terminal.LineFeed();
  ASSERT(cr_fixture.FlushOutput() == "\r");
}

/**
 * @brief 测试项函数 `TestHeaderAndClearSequences`。 Test-item function `TestHeaderAndClearSequences`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestHeaderAndClearSequences()
{
  // 测试内容：验证 header、清行和清屏输出的精确 escape 序列。
  // Test coverage: verify the exact escape sequences for header, clear-line, and clear-screen output.
  TerminalDisplayFixture<LibXR::Terminal<>::Mode::CRLF> fixture;

  fixture.terminal.ShowHeader();
  ASSERT(fixture.FlushOutput() == "ramfs:/$ ");

  auto dir = LibXR::RamFS::CreateDir("dir1");
  fixture.ramfs.Add(dir);
  fixture.terminal.current_dir_ = &dir;
  fixture.terminal.ShowHeader();
  ASSERT(fixture.FlushOutput() == "ramfs:dir1$ ");

  fixture.terminal.ClearLine();
  ASSERT(fixture.FlushOutput() == "\033[2K\r");

  fixture.terminal.Clear();
  ASSERT(fixture.FlushOutput() == "\033[2J\033[1H");
}

}  // namespace

/**
 * @brief 测试项函数 `RunTerminalDisplayModeTests`。 Test-item function `RunTerminalDisplayModeTests`.
 * @details 测试内容：执行 `Terminal` 换行与清屏显示子场景。 Execute `Terminal` line-feed and clear-sequence display sub-scenarios.
 *          测试原理：把纯输出序列语义单独成组，避免与历史和行内编辑场景缠绕。 Group pure output-sequence semantics away from history and inline-edit scenarios.
 */
void RunTerminalDisplayModeTests()
{
  TestLineFeedModes();
  TestHeaderAndClearSequences();
}
