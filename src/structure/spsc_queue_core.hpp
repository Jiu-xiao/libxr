#pragma once

#include "detail/sp_queue_core_impl.hpp"

namespace LibXR
{
/**
 * @class SPSCQueueCore
 * @brief 单生产者单消费者队列内核 / Single-producer single-consumer queue core
 */
class SPSCQueueCore : public Detail::SPQueueCoreImpl<false>
{
 public:
  /**
   * @brief 继承共享单生产者内核的构造函数 / Inherit constructors from the shared single-producer core
   */
  using Detail::SPQueueCoreImpl<false>::SPQueueCoreImpl;
};
}  // namespace LibXR
