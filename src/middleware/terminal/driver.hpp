/**
 * @file
 * @brief 终端线程与轮询入口 / Thread and polling entry points for Terminal.
 */

/**
 * @brief 在线程中读取输入并驱动终端 / Read input and drive the terminal in a thread.
 * @param term 终端实例 / Terminal instance.
 * @note 无数据时通过零长度 BLOCK 读等待队列非空；取得输入后，在 write_mutex_ 保护下
 *       解析并提交输出。
 *       With no data, a zero-length BLOCK read waits for a nonempty queue. Received
 *       input is parsed and output committed under write_mutex_.
 */
static void ThreadFun(Terminal* term)
{
  Semaphore read_sem, write_sem;
  ReadOperation op(read_sem);

  term->write_op_ = WriteOperation(write_sem, 10);

  while (true)
  {
    term->request_read_size_ = LibXR::min(term->read_port_->Size(), READ_BUFF_SIZE);
    auto buffer = RawData(term->read_buff_, term->request_read_size_);

    if ((*term->read_port_)(buffer, op) == ErrorCode::OK && term->request_read_size_ > 0)
    {
      term->write_mutex_->Lock();
      term->Parse(buffer);
      term->write_stream_.Commit();
      term->write_mutex_->Unlock();
    }
  }
}

/**
 * @brief 检查读取结果并推进终端输入 / Check read completion and advance terminal input.
 * @param term 终端实例 / Terminal instance.
 * @note 首次调用发起读取；RUNNING 时返回，DONE 时解析并提交输出，再发起下次读取。
 *       ERROR 时重新读取。解析和输出提交受 write_mutex_ 保护。
 *       Starts a read on first use; returns for RUNNING. On DONE, parses and commits
 *       output, then starts the next read. On ERROR, restarts reading. Parsing and
 *       output submission hold write_mutex_.
 */
static void TaskFun(Terminal* term)
{
  ReadOperation op(term->read_status_);

  auto start_read = [&]()
  {
    term->request_read_size_ =
        LibXR::min(LibXR::max(1u, term->read_port_->Size()), READ_BUFF_SIZE);
    auto buffer = RawData(term->read_buff_, term->request_read_size_);
    (*term->read_port_)(buffer, op);
  };

  while (true)
  {
    const auto status = term->read_status_.load(std::memory_order_acquire);
    switch (status)
    {
      case ReadOperation::OperationPollingStatus::READY:
      {
        term->request_read_size_ =
            LibXR::min(LibXR::max(1u, term->read_port_->Size()), READ_BUFF_SIZE);
        auto buffer = RawData(term->read_buff_, term->request_read_size_);
        (*term->read_port_)(buffer, op);
        continue;
      }
      case ReadOperation::OperationPollingStatus::RUNNING:
        return;
      case ReadOperation::OperationPollingStatus::DONE:
      {
        term->write_mutex_->Lock();
        auto buffer = RawData(term->read_buff_, term->request_read_size_);
        term->Parse(buffer);
        term->write_stream_.Commit();
        term->write_mutex_->Unlock();
        start_read();
        return;
      }
      case ReadOperation::OperationPollingStatus::ERROR:
      {
        start_read();
        return;
      }
    }
  }
}
