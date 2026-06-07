#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "spmc_queue.hpp"
#include "operation.hpp"

namespace LibXR
{

/**
 * @brief WritePort class for handling write operations.
 * @brief 处理写入操作的WritePort类。
 */
class WritePort
{
 public:
  // Exposed low-level state and helpers for the write-path core. Some members stay public
  // because low-level libxr tests and backend glue inspect them directly. Keep the
  // boundary explicit instead of introducing a fake private wall that tests cannot use.
  // 写路径核心的低层状态与辅助类型。部分成员保持 public，
  // 是因为 libxr 的底层测试与后端胶水层会直接检查它们。
  // 这里保持显式边界即可，不做测试本身也用不上的“伪私有化”。

  // Write BLOCK states:
  // LOCKED = submit path owns queue mutation
  // BLOCK_PUBLISHING = BLOCK submit path is publishing queue metadata
  // BLOCK_WAITING = waiter armed, completion not claimed yet
  // BLOCK_CLAIMED = final wakeup belongs to the waiter
  // BLOCK_DETACHED = timeout detached the waiter
  // RESETTING = fail-and-clear path owns queue mutation
  // The same semaphore may be reused only after the previous BLOCK call
  // returns and the port goes back to IDLE.
  // 写 BLOCK 状态：
  // LOCKED = 提交路径占有队列修改权
  // BLOCK_PUBLISHING = BLOCK 提交路径正在发布队列元数据
  // BLOCK_WAITING = waiter 已挂起，完成尚未 claim
  // BLOCK_CLAIMED = 最终唤醒已经归 waiter 所有
  // BLOCK_DETACHED = timeout 已把 waiter 分离
  // RESETTING = fail-and-clear 路径占有队列修改权
  // 同一个信号量只能在上一次 BLOCK 调用返回、端口回到 IDLE 后复用。
  enum class BusyState : uint32_t
  {
    LOCKED =
        0,  ///< Submission path owns queue mutation. 提交路径占有写队列/元数据修改权。
    BLOCK_PUBLISHING = 1,  ///< BLOCK submitter is publishing queue metadata.
                           ///< BLOCK 提交者正在发布队列元数据。
    BLOCK_WAITING = 2,  ///< One BLOCK waiter is armed and waiting for final completion.
                        ///< 一个 BLOCK 等待者已经挂起，等待最终完成。
    BLOCK_CLAIMED =
        3,  ///< Final wakeup belongs to the current waiter. 最终唤醒已归当前等待者所有。
    BLOCK_DETACHED = 4,  ///< Waiter already timed out/detached; completion must not post.
                         ///< 等待者已超时/被分离，完成侧不能再 Post。
    RESETTING = 5,        ///< Fail-and-clear owns queue mutation.
                          ///< Fail-and-clear 路径占有写队列/元数据修改权。
    IDLE = UINT32_MAX    ///< No active submitter and no armed BLOCK waiter.
                         ///< 没有活动提交者，也没有挂起中的 BLOCK 等待者。
  };

  WriteFun write_fun_ = nullptr;  ///< Driver/backend write entry. 底层驱动或后端写入入口。
  SPMCQueue<WriteInfoBlock>* queue_info_ =
      nullptr;  ///< Metadata queue for pending write batches. 挂起写批次的元数据队列。
  SPMCQueue<uint8_t>* queue_data_ =
      nullptr;  ///< Payload queue for pending write bytes. 挂起写入字节的数据队列。
  std::atomic<BusyState> busy_{BusyState::IDLE};  ///< Shared submit/wait handoff state. 共享的提交/等待交接状态。
  ErrorCode block_result_ = ErrorCode::OK;  ///< Final status for the current BLOCK write. 当前 BLOCK 写入的最终结果。

  // Stream batch facade.
  // Stream 负责一次批次的累积写入与提交。
  class Stream
  {
   public:
    /**
     * @brief 构造流写入对象，并尝试锁定端口。
     * @brief Constructs a Stream object and tries to acquire WritePort lock.
     * @param port 指向 WritePort 的指针 Pointer to WritePort.
     * @param op 写操作对象（可重用）Write operation object (can be reused).
     */
    Stream(LibXR::WritePort* port, LibXR::WriteOperation op);

    /**
     * @brief 析构时自动提交已累积的数据并释放锁。
     * @brief Destructor: automatically commits any accumulated data and releases the
     * lock.
     */
    ~Stream();

    /**
     * @brief 追加一个原始数据片段到当前流批次。
     * @brief Appends one raw-data chunk to the current stream batch.
     *
     * 该接口是 Stream 的底层语义写入入口。它负责拿到当前批次的写入所有权，并尝试将
     * 整个片段原子地追加到当前批次对应的共享 data queue 尾部；Commit() 负责随后发布
     * 这批字节对应的元数据。若空间不足则返回 FULL，并保持该片段完全未写入。
     * This is the low-level semantic write entrypoint of Stream. It acquires the current
     * batch ownership and then attempts to append the whole chunk atomically into the
     * shared data-queue tail; Commit() later publishes the metadata that describes this
     * batch. If space is insufficient it returns FULL and leaves that chunk entirely
     * unwritten.
     *
     * @param data 要写入的数据片段 Raw-data chunk to append.
     * @return 返回操作结果。Returns the write result.
     */
    [[nodiscard]] ErrorCode Write(ConstRawData data);

    /**
     * @brief 追加一个文本片段到当前流批次。
     * @brief Appends one text chunk to the current stream batch.
     * @param text 要写入的文本片段 Text chunk to append.
     * @return 返回操作结果。Returns the write result.
     */
    [[nodiscard]] ErrorCode Write(std::string_view text)
    {
      return Write(ConstRawData{text.data(), text.size()});
    }

    /**
     * @brief 追加写入数据的语法糖，忽略返回状态并支持链式调用。
     * @brief Syntax sugar for appending data; ignores the status and supports chaining.
     * @param data 要写入的数据 Data to write.
     * @return 返回自身引用 Enables chainable call.
     */
    Stream& operator<<(const ConstRawData& data);

    /**
     * @brief 手动提交已写入的数据到队列，并释放当前锁。
     * @brief Manually commit accumulated data to the queue, then release the current
     * lock.
     *
     * 调用后会发布当前批次对应的元数据、使这批已追加到共享 data queue 的字节正式成为
     * 一个可消费的写操作，并将 size 计数归零。适合周期性手动 flush。
     * After calling, the metadata that describes the current batch is published so the
     * bytes already appended into the shared data queue become one consumable write
     * operation, and the size counter is reset. Suitable for periodic manual flush.
     *
     * @return 返回操作的 ErrorCode，指示操作结果。
     *         Returns an ErrorCode indicating the result of the operation.
     */
    ErrorCode Commit();

    /**
     * @brief 为当前流批次获取一次可写入的端口所有权。
     * @brief Acquires append ownership for the current stream batch.
     * @return 返回获取结果。Returns the acquisition result.
     */
    [[nodiscard]] ErrorCode Acquire();

    /**
     * @brief 获取当前批次还可追加的剩余字节数。
     * @brief Returns the remaining appendable bytes in the current batch.
     *
     * 返回值只在 Acquire 成功后有意义；若当前尚未持有流批次所有权，则返回 0。
     * The return value is meaningful only after Acquire succeeds; it returns zero while
     * this stream does not currently own the batch.
     */
    [[nodiscard]] size_t EmptySize() const
    {
      return owns_port_ ? (batch_capacity_ - buffered_size_) : 0;
    }

   private:
    /**
     * @brief 将当前已缓存批次提交给 WritePort。
     * @brief Submits the currently buffered batch to WritePort.
     *
     * 非 BLOCK 路径下，提交后当前 Stream 仍负责释放端口所有权；BLOCK 路径下，
     * 所有权会交给 WritePort 的等待状态机继续管理。
     * On non-BLOCK paths, this Stream still releases the port ownership after submission;
     * on BLOCK paths, ownership is handed off to WritePort's wait-state machine.
     */
    [[nodiscard]] ErrorCode SubmitBuffered();

    /**
     * @brief 将当前批次的端口所有权归还给 WritePort。
     * @brief Releases the current batch ownership back to WritePort.
     */
    void Release();

    LibXR::WritePort* port_;  ///< 写端口指针 Pointer to the WritePort
    LibXR::WriteOperation op_;  ///< 写操作对象 Write operation object
    size_t batch_capacity_ = 0;  ///< 当前批次可用的总容量 Total capacity reserved for the current batch
    size_t buffered_size_ = 0;  ///< 当前批次已追加到共享 data queue、但尚未发布对应元数据的字节数 Bytes already appended into the shared data queue for the current batch, but whose metadata has not yet been published
    bool owns_port_ = false;  ///< 当前 Stream 是否持有该批次的端口所有权 Whether this Stream currently owns the batch
  };

  /**
   * @brief 构造一个新的 WritePort 对象。
   *        Constructs a new WritePort object.
   *
   * 该构造函数初始化无锁操作队列 queue_info_ 和数据块队列 queue_data_。
   * This constructor initializes the lock-free operation queue queue_info_ and the data
   * block queue queue_data_.
   *
   * @param queue_size 队列的大小，默认为 3。
   *                   The size of the queue, default is 3.
   * @param buffer_size 缓存数据字节队列的容量，默认为 128。
   *                    Capacity of the cached-byte queue, default is 128.
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   */
  WritePort(size_t queue_size = 3, size_t buffer_size = 128);

  /**
   * @brief 获取数据队列的剩余可用空间。
   *        Gets the remaining available space in the data queue.
   *
   * @return 返回数据队列的空闲大小。
   *         Returns the empty size of the data queue.
   */
  size_t EmptySize();

  /**
   * @brief 获取当前数据队列的已使用大小。
   *        Gets the used size of the current data queue.
   *
   * @return 返回数据队列的已使用大小。
   *         Returns the size of the data queue.
   */
  size_t Size();

  /**
   * @brief 判断端口是否可写。
   *        Checks whether the port is writable.
   *
   * @return 如果 write_fun_ 不为空，则返回 true，否则返回 false。
   *         Returns true if write_fun_ is not null, otherwise returns false.
   */
  bool Writable();

  /**
   * @brief 赋值运算符重载，用于设置写入函数。
   *        Overloaded assignment operator to set the write function.
   *
   * 该函数允许使用 WriteFun 类型的函数对象赋值给 WritePort，从而设置 write_fun_。
   * This function allows assigning a WriteFun function object to WritePort, setting
   * write_fun_.
   *
   * @param fun 要分配的写入函数。
   *            The write function to be assigned.
   * @return 返回自身的引用，以支持链式调用。
   *         Returns a reference to itself for chaining.
   */
  WritePort& operator=(WriteFun fun);

  /**
   * @brief 更新写入操作的状态。
   *        Updates the status of the write operation.
   *
   * 该函数在写入操作完成时更新对应 info 的状态，并调用 UpdateStatus 更新 op。
   * This function updates the status stored in info and calls UpdateStatus on op when a
   * write operation completes.
   *
   * @param in_isr 指示是否在中断上下文中执行。
   *               Indicates whether the operation is executed in an interrupt context.
   * @param ans 错误码，用于指示操作的结果。
   *            Error code indicating the result of the operation.
   * @param op 需要更新状态的 WriteOperation 引用。
   *           Reference to the WriteOperation whose status needs to be updated.
   */
  void Finish(bool in_isr, ErrorCode ans, WriteInfoBlock& info);

  /**
   * @brief 标记写入操作为运行中。
   *        Marks the write operation as running.
   *
   * 该函数用于将 op 标记为运行状态，以指示当前正在进行写入操作。
   * This function marks op as running to indicate an ongoing write operation.
   *
   * @param op 需要更新状态的 WriteOperation 引用。
   *           Reference to the WriteOperation whose status needs to be updated.
   */
  void MarkAsRunning(WriteOperation& op);

  /**
   * @brief 执行写入操作。
   *        Performs a write operation.
   *
   * 该函数检查端口是否可写，并根据 data.size_ 和 op 的类型执行不同的操作。
   * This function checks if the port is writable and performs different actions based on
   * data.size_ and the type of op.
   *
   * @param data 需要写入的原始数据。
   *             Raw data to be written.
   * @param op 写入操作对象，包含操作类型和同步机制。
   *           Write operation object containing the operation type and synchronization
   * mechanism.
   * @param in_isr 指示是否在中断上下文中执行。
   *               Indicates whether the operation is executed in an interrupt context.
   * @return 返回操作的 ErrorCode，指示操作结果。
   *         Returns an ErrorCode indicating the result of the operation.
   */
  ErrorCode operator()(ConstRawData data, WriteOperation& op, bool in_isr = false);

  /**
   * @brief 失败完成并清空当前所有挂起写操作。
   * @brief Fail-complete and clear all currently pending write operations.
   *
   * @note Driver-only: call this only after the backend is known to be unavailable.
   * @note 仅供驱动层在后端已明确不可用后调用。
   * @note The surrounding driver must already guarantee that no new front-end
   *       submissions or back-end completion/data events can still arrive for
   *       this port.
   * @note 外围驱动还必须先保证：这条端口后续不会再收到新的前端提交，也不会再收到
   *       新的后端完成或数据事件。
   * @note Seeing LOCKED / BLOCK_PUBLISHING / RESETTING here means the caller
   *       violated that driver-side precondition.
   * @note 若此时仍看到 LOCKED / BLOCK_PUBLISHING / RESETTING，说明调用方违反了
   *       上述驱动层前提。
   * @note Dev builds fire DEV_ASSERT, while release builds still back off.
   * @note 开发期会触发 DEV_ASSERT，发布构建仍保持直接退回。
   *
   * @param reason 最终失败原因 / Final failure reason
   * @param in_isr 是否在 ISR 上下文 / Whether running in ISR context
   */
  void FailAndClearAll(ErrorCode reason, bool in_isr);

  /**
   * @brief 提交写入操作。
   *        Commits a write operation.
   *
   * @param data 写入的原始数据 / Raw data to be written
   * @param op 写入操作对象，包含操作类型和同步机制。
   *           Write operation object containing the operation type and synchronization
   * @param pushed 数据是否已经推送到缓冲区 / Whether the data has been pushed to the
   * buffer
   * @param in_isr 指示是否在中断上下文中执行。
   *               Indicates whether the operation is executed in an interrupt context.
   * @return 返回操作的 ErrorCode，指示操作结果。
   *         Returns an ErrorCode indicating the result of the operation.
   */
  ErrorCode CommitWrite(ConstRawData data, WriteOperation& op, bool pushed = false,
                        bool in_isr = false);
};

}  // namespace LibXR
