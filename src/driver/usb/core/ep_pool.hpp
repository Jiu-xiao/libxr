#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "ep.hpp"
#include "lockfree_pool.hpp"

namespace LibXR::USB
{

/**
 * @brief USB端点池类 / USB endpoint pool class
 *
 * 继承自 LockFreePool<Endpoint*>，用于高效管理 USB 端点对象指针。
 * 所有 Endpoint* 在池初始化时填充，池只负责分配、查找与回收，不负责对象生命周期。
 *
 * Inherited from LockFreePool<Endpoint*>, this pool manages fixed Endpoint pointers.
 * All Endpoint objects must be valid during the pool's lifetime.
 */
class EndpointPool : protected LockFreePool<Endpoint*>
{
 public:
  using LockFreePool<Endpoint*>::Put;

  /**
   * @brief 构造函数 / Constructor
   * @param endpoint_num 端点总数（包含端点0，必须 >=2）/ Total number of endpoints
   * (including EP0, must >=2)
   */
  EndpointPool(size_t endpoint_num);

  /**
   * @brief 分配端点 / Allocate endpoint
   * @param[out] ep_info 分配得到的端点对象指针 / Allocated endpoint pointer
   * @param direction 端点方向（IN/OUT/BOTH）/ Endpoint direction
   * @param ep_num 指定端点号/ Endpoint number
   * 0)
   * @retval ErrorCode 操作结果（OK/NOT_FOUND）/ Operation result
   */
  ErrorCode Get(Endpoint*& ep_info, Endpoint::Direction direction,
                Endpoint::EPNumber ep_num = Endpoint::EPNumber::EP_AUTO);

  /**
   * @brief 回收端点 / Release endpoint
   * @param ep_info 待回收的端点对象指针 / Endpoint pointer to release
   * @retval ErrorCode 操作结果（OK/NOT_FOUND）/ Operation result
   */
  ErrorCode Release(Endpoint* ep_info);

  /**
   * @brief 查找端点/ Lookup endpoint
   * @param ep_addr 端点地址，IN端点高位需加0x80 / Endpoint address (0x80 for IN
   * endpoints)
   * @param[out] ans 查找到的端点对象指针 / Found endpoint pointer
   * @retval ErrorCode 操作结果（OK/NOT_FOUND）/ Operation result
   */
  ErrorCode FindEndpoint(uint8_t ep_addr, Endpoint*& ans);

  /**
   * @brief 获取端点0的OUT对象 / Get Endpoint 0's OUT object
   * @return Endpoint* 端点0 OUT对象指针 / Endpoint 0 OUT pointer
   */
  Endpoint* GetEndpoint0Out();

  /**
   * @brief 获取端点0的IN对象 / Get Endpoint 0's IN object
   * @return Endpoint* 端点0 IN对象指针 / Endpoint 0 IN pointer
   */
  Endpoint* GetEndpoint0In();

  /**
   * @brief 设置端点0的IN/OUT对象 / Set Endpoint 0 IN/OUT objects
   * @param ep0_in 端点0 IN对象指针 / Endpoint 0 IN pointer
   * @param ep0_out 端点0 OUT对象指针 / Endpoint 0 OUT pointer
   */
  void SetEndpoint0(Endpoint* ep0_in, Endpoint* ep0_out);

 private:
  Endpoint* ep0_in_ = nullptr;   ///< 端点0 IN对象 / Endpoint 0 IN pointer
  Endpoint* ep0_out_ = nullptr;  ///< 端点0 OUT对象 / Endpoint 0 OUT pointer
};

}  // namespace LibXR::USB
