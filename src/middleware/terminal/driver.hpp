  /**
   * @brief  `Terminal` 的驱动入口片段
   *         Driver-entry fragment of `Terminal`
   *
   * @note 这一组函数只决定“什么时候读、什么时候解析、什么时候提交输出”，不负责
   *       行编辑、命令语义或显示细节。
   *       This group decides only when to read, when to parse, and when to
   *       commit output; it does not own line editing, command semantics, or
   *       display details.
   */

  /**
   * @brief  终端线程函数，以独立线程方式持续驱动终端
   *         Terminal thread function, continuously drives the terminal as an independent
   * thread
   *
   * @details
   * 该函数用于以独立线程的方式驱动终端，持续从输入流读取数据并解析，适用于高实时性应用。
   * 它会在循环中不断检查输入流的大小，并在数据可用时进行解析。
   *
   * This function runs as a separate thread to continuously drive the terminal,
   * reading and parsing input data in a loop. It is suitable for high real-time
   * applications. It continuously checks the input stream size and processes data when
   * available.
   *
   * @param  term 指向 Terminal 实例的指针 Pointer to the Terminal instance
   * @note 每次真正拿到一批输入后，解析和输出提交都在 `write_mutex_` 保护下完成，
   *       以免和外部写口共享同一个输出流时交错。
   *       Once one input batch is actually obtained, parsing and output commit
   *       are both completed under `write_mutex_` so they do not interleave
   *       with other writers sharing the same output stream.
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

      if ((*term->read_port_)(buffer, op) == ErrorCode::OK &&
          term->request_read_size_ > 0)
      {
        term->write_mutex_->Lock();
        term->Parse(buffer);
        term->write_stream_.Commit();
        term->write_mutex_->Unlock();
      }
    }
  }

  /**
   * @brief  终端任务函数，以定时器任务方式驱动终端
   *         Terminal task function, drives the terminal using a scheduled task
   *
   * @details
   * 该函数用于以定时任务（或轮询方式）驱动终端，适用于资源受限的系统。
   * 它不会持续运行，而是在定时器触发或系统任务调度时运行，执行一次数据读取和解析后返回。
   *
   * This function drives the terminal using a scheduled task (or polling mode),
   * making it suitable for resource-constrained systems.
   * Unlike the thread-based approach, it only runs when scheduled
   * (e.g., triggered by a timer) and processes available input data before returning.
   *
   * @param  term 指向 Terminal 实例的指针 Pointer to the Terminal instance
   * @note `TaskFun()` 每次只推进有限一步状态机：发起读取、等待完成、消费完成结果，
   *       然后立即返回给调度方。
   *       `TaskFun()` advances the state machine by only a bounded step each
   *       time: start one read, wait for completion, consume one completed
   *       result, then return to the scheduler immediately.
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
      switch (term->read_status_)
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
