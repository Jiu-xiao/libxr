#pragma once

#include "../topic.hpp"

namespace LibXR
{
namespace Detail::MessageSubscriber
{
/**
 * @brief 校验订阅者接收类型是否满足当前主题的精确类型契约 / Check whether the subscriber
 *        receive type satisfies the topic's exact type contract
 * @tparam Data 待校验的数据类型 / Data type to validate
 * @param topic 待校验的主题 / Topic to validate against
 *
 * @note 当前 message bus 只允许精确类型契约：
 *       订阅类型必须和 topic 绑定的 payload 类型完全一致 /
 *       The current message bus allows exact type contracts only:
 *       the subscriber type must exactly match the topic payload type
 */
template <typename Data>
void CheckSubscriberType(Topic topic)
{
  CheckTopicPayload<Data>();
  auto topic_handle = static_cast<Topic::TopicHandle>(topic);
  ASSERT(topic_handle->data_.payload_type_id == TypeID::GetID<Data>());
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

}  // namespace LibXR
