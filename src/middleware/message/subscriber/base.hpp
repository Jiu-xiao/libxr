#pragma once

#include "../topic.hpp"

namespace LibXR
{
namespace Detail::MessageSubscriber
{
/**
 * @brief 校验订阅者接收类型是否满足当前主题的尺寸规则 / Check whether the subscriber
 *        receive type satisfies the current topic size rule
 * @tparam Data 待校验的数据类型 / Data type to validate
 * @param topic 待校验的主题 / Topic to validate against
 *
 * @note 若主题启用了 `check_length`，订阅类型必须与主题负载等长；否则允许订阅类型更大，
 *       只要求它至少能容纳主题当前声明的最大负载前缀 /
 *       When the topic enables `check_length`, the subscriber type must have the
 *       exact same size as the topic payload; otherwise a larger subscriber type
 *       is allowed as long as it can hold at least the declared payload prefix
 */
template <typename Data>
void CheckSubscriberDataSize(Topic topic)
{
  CheckTopicPayload<Data>();
  auto topic_handle = static_cast<Topic::TopicHandle>(topic);
  if (topic_handle->data_.check_length)
  {
    ASSERT(topic_handle->data_.max_length == sizeof(Data));
  }
  else
  {
    ASSERT(topic_handle->data_.max_length <= sizeof(Data));
  }
}

/**
 * @brief 为异步订阅者分配一份长期保留的本地接收对象 / Allocate one long-lived local
 *        receive object for an async subscriber
 * @tparam Data 接收缓冲区对应的数据类型 / Data type stored in the receive buffer
 * @return 新分配的接收缓冲区视图 / Returns the newly allocated receive buffer view
 * @note 这里会实际分配一个 `Data` 对象，并把它的地址包装成 `RawData` 返回；这块对象
 *       预期会跟着订阅者一起长期保留 /
 *       This allocates one `Data` object and returns its address as `RawData`;
 *       the object is expected to stay alive together with the subscriber
 */
template <typename Data>
RawData NewSubscriberBuffer()
{
  CheckTopicPayload<Data>();
  auto* data = new Data;
  return RawData(*data);
}
}  // namespace Detail::MessageSubscriber

/**
 * @enum Topic::SuberType
 * @brief 这一条订阅记录属于哪种订阅方式 / Which subscription style one subscriber record
 *        uses
 */
enum class Topic::SuberType : uint8_t
{
  SYNC,      ///< 同步订阅者。Synchronous subscriber.
  ASYNC,     ///< 异步订阅者。Asynchronous subscriber.
  QUEUE,     ///< 队列订阅者。Queued subscriber.
  CALLBACK,  ///< 回调订阅者。Callback subscriber.
};

/**
 * @struct Topic::SuberBlock
 * @brief 每种订阅块前面都有的公共头 / Common header stored at the front of every
 *        subscriber block
 */
struct Topic::SuberBlock
{
  SuberType type;  ///< 订阅者类型。Type of subscriber.
};
}  // namespace LibXR
