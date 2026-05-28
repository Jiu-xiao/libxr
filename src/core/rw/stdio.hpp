#pragma once

#include <cstddef>
#include <string_view>
#include <tuple>
#include <utility>

#include "mutex.hpp"
#include "print.hpp"
#include "read_port.hpp"
#include "write_port.hpp"

namespace LibXR
{

/**
 * @brief STDIO interface for read/write ports.
 * @brief 提供静态全局的输入输出接口绑定与写会话管理。
 */
class STDIO
{
 public:
  // Shared global stdio binding state.
  // 共享的全局 stdio 绑定状态。
  // NOLINTBEGIN
  static inline ReadPort* read_ = nullptr;    ///< Read port instance. 读取端口。
  static inline WritePort* write_ = nullptr;  ///< Write port instance. 写入端口。
  static inline LibXR::Mutex* write_mutex_ =
      nullptr;  ///< Write port mutex. 写入端口互斥锁。
  static inline LibXR::WritePort::Stream* write_stream_ =
      nullptr;  ///< Optional externally owned write stream. 可选的外部托管写流。
  // NOLINTEND

 private:
  // Compiled-format bridge shared by the brace and printf frontends.
  // brace 与 printf 两个前端共用的编译格式桥接层。

  /**
   * @brief STDIO 编译格式会话使用的流式截断输出端。
   * @brief Stream-backed truncating sink used by one STDIO compiled-format session.
   */
  class CompiledSink
  {
   public:
    /**
     * @brief 构造一个绑定到指定流的编译格式输出端。
     * @brief Constructs one compiled-format sink bound to the given stream.
     */
    explicit CompiledSink(WritePort::Stream& stream);

    /**
     * @brief 追加一个文本片段；必要时按会话剩余空间直接截断。
     * @brief Appends one text chunk and truncates directly to the remaining session
     * capacity when needed.
     */
    [[nodiscard]] ErrorCode Write(std::string_view chunk);

    /**
     * @brief 返回当前会话最终保留下来的字节数。
     * @brief Returns the retained byte count of the current session.
     */
    [[nodiscard]] size_t RetainedSize() const { return retained_size_; }

   private:
    WritePort::Stream& stream_;  ///< Active stream session receiving retained bytes. 接收保留字节的活动流会话。
    size_t retained_size_ = 0;  ///< Bytes retained so far. 当前已保留的字节数。
    bool saturated_ = false;  ///< No more bytes should be retained in this session. 当前会话不再继续保留输出。
  };

  /// @brief Type-erased bridge for one compiled STDIO call. / 一次编译格式 STDIO 调用的类型擦除桥接函数。
  using CompiledWriteFun = ErrorCode (*)(void* context, CompiledSink& sink);

  /**
   * @brief 一次编译格式 STDIO 调用的模板上下文。
   * @brief Template context for one compiled-format STDIO call.
   */
  template <typename CompiledFormat, typename... Args>
  struct CompiledCall
  {
    const CompiledFormat& format;  ///< Compile-time compiled format object. 编译期已编译的格式对象。
    std::tuple<Args&&...> args;  ///< Forwarded runtime arguments. 转发保存的运行时参数。

    /**
     * @brief 将当前模板上下文桥接到编译格式前端写入入口。
     * @brief Bridges the current template context into the compiled-format write entry.
     */
    [[nodiscard]] static ErrorCode Write(void* context, CompiledSink& sink)
    {
      auto& compiled_call = *static_cast<CompiledCall*>(context);
      return std::apply(
          [&](auto&&... unpacked) -> ErrorCode
          {
            return Print::Write(sink, compiled_call.format,
                                std::forward<decltype(unpacked)>(unpacked)...);
          },
          compiled_call.args);
    }
  };

  /**
   * @brief 在指定 Stream 上执行一次完整的 STDIO 编译格式写入与收尾。
   * @brief Runs one complete STDIO compiled-format write/finalize pass on the given
   * Stream.
   *
   * 该 helper 统一负责：构造 CompiledSink、调用前端桥接函数、再用
   * FinishWriteSession() 做最终提交或丢弃。
   * This helper centralizes: constructing CompiledSink, invoking the frontend bridge,
   * then finalizing through FinishWriteSession().
   */
  [[nodiscard]] static int WriteCompiledToStream(WritePort::Stream& stream,
                                                 void* context,
                                                 CompiledWriteFun write_fun);

  /**
   * @brief 执行一次完整的 STDIO 编译格式流会话选择、写入与收尾。
   * @brief Runs one complete STDIO compiled-format stream session: stream selection,
   * write, and finalization.
   *
   * 若当前已存在外部绑定的 write_stream_，则复用它；否则创建一个临时的
   * WritePort::Stream 供本次会话使用。
   * Reuses the externally bound write_stream_ when available; otherwise creates one
   * temporary WritePort::Stream for the current session.
   */
  [[nodiscard]] static int WriteCompiledSession(void* context, CompiledWriteFun write_fun);

  /**
   * @brief 执行一次模板已知的 STDIO 编译格式会话。
   * @brief Runs one STDIO compiled-format session whose format/argument types are already
   * known at compile time.
   *
   * 该 helper 只保留模板相关的最薄一层：拿共享会话，再把类型化调用对象交给
   * WriteCompiledSession()。
   * This helper keeps only the thinnest template layer: acquire the shared session, then
   * pass the typed call object into WriteCompiledSession().
   */
  template <typename Call>
  [[nodiscard]] static int RunCompiledSession(Call& call)
  {
    if (!BeginWriteSession())
    {
      return -1;
    }

    return WriteCompiledSession(&call, &Call::Write);
  }

  /**
   * @brief 用一份已编译格式和一组运行时参数执行一次完整的 STDIO 写会话。
   * @brief Runs one complete STDIO write session with one compiled format and one
   * runtime argument pack.
   */
  template <typename CompiledFormat, typename... Args>
  [[nodiscard]] static int RunCompiled(const CompiledFormat& format, Args&&... args)
  {
    CompiledCall<CompiledFormat, Args...> call{
        format, std::forward_as_tuple(std::forward<Args>(args)...)};
    return RunCompiledSession(call);
  }

  /**
   * @brief 获取一个共享的 STDIO 写入会话。
   * @brief Acquires one shared STDIO write session.
   */
  [[nodiscard]] static bool BeginWriteSession();

  /**
   * @brief 提交当前编译格式会话的写入流并释放共享会话。
   * @brief Commits the current compiled-format session stream and releases the shared
   * session.
   */
  [[nodiscard]] static int FinishWriteSession(WritePort::Stream& stream,
                                              size_t retained_size,
                                              ErrorCode format_result);

 public:

  /**
   * @brief Prints one compile-time brace literal to the active STDIO output.
   * @brief 将一个编译期 brace 字面量打印到当前 STDIO 输出。
   * @return Returns the byte count actually retained and committed to the
   *         current STDIO stream; returns -1 on session or commit failure.
   *         返回当前 STDIO 流实际保留并提交的字节数；若会话建立或提交失败，
   *         则返回 -1。
   */
  template <Print::Text Source, typename... Args>
  static int Print(Args&&... args)
  {
    constexpr LibXR::Format<Source> format{};
    return RunCompiled(format, std::forward<Args>(args)...);
  }

  /**
   * @brief Prints one compile-time printf literal to the active STDIO output.
   * @brief 将一个编译期 printf 字面量打印到当前 STDIO 输出。
   * @return Returns the byte count actually retained and committed to the
   *         current STDIO stream; returns -1 on session or commit failure.
   *         返回当前 STDIO 流实际保留并提交的字节数；若会话建立或提交失败，
   *         则返回 -1。
   */
  template <Print::Text Source, typename... Args>
  static int Printf(Args&&... args)
  {
    constexpr auto format = Print::Printf::Build<Source>();
    return RunCompiled(format, std::forward<Args>(args)...);
  }
};

}  // namespace LibXR
