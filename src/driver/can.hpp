#pragma once

#include "libxr.hpp"
#include "message.hpp"

namespace LibXR
{

/**
 * @brief CAN通信接口，定义标准CAN通信结构，支持不同类型的消息 (CAN communication
 * interface that defines a standard CAN structure supporting different message types).
 */
class CAN
{
 public:
  /**
   * @brief CAN 消息类型 (Enumeration of CAN message types).
   */
  enum class Type : uint8_t
  {
    STANDARD = 0,         ///< 标准 CAN 消息 (Standard CAN message).
    EXTENDED = 1,         ///< 扩展 CAN 消息 (Extended CAN message).
    REMOTE_STANDARD = 2,  ///< 远程标准 CAN 消息 (Remote standard CAN message).
    REMOTE_EXTENDED = 3   ///< 远程扩展 CAN 消息 (Remote extended CAN message).
  };

  /**
   * @brief 构造 CAN 对象，可指定主题名称和通信域 (Constructs a CAN object with an
   * optional topic name and domain).
   * @param name_tp CAN 消息的主题名称 (Topic name for CAN messages).
   * @param domain 可选的通信域 (Optional domain for message communication).
   */
  CAN(const char *name_tp = "can", Topic::Domain *domain = nullptr)
      : classic_tp_(name_tp, sizeof(ClassicPack), domain, false, true)
  {
  }

  /**
   * @brief 经典 CAN 消息结构 (Structure representing a classic CAN message).
   */
  typedef union
  {
    struct __attribute__((packed))
    {
      uint32_t id;      ///< 消息 ID (Message ID).
      Type type;        ///< 消息类型 (Message type).
      uint8_t data[8];  ///< 数据载荷，最大 8 字节 (Data payload, max 8 bytes).
    };
    uint8_t raw[12];  ///< 原始字节数组表示 (Raw byte array representation).
  } ClassicPack;

  Topic classic_tp_;  ///< 经典 CAN 消息的主题 (Topic for classic CAN messages).

  /**
   * @brief 添加 CAN 消息到系统 (Adds a CAN message to the system).
   * @param pack 经典 CAN 消息包 (The classic CAN message packet).
   * @return 操作结果 (ErrorCode indicating success or failure).
   */
  virtual ErrorCode AddMessage(const ClassicPack &pack) = 0;
};

/**
 * @brief FDCAN 通信接口，扩展 CAN 功能，支持灵活数据速率（FD）CAN 消息 (FDCAN
 * communication interface that extends CAN functionality by supporting Flexible Data-Rate
 * (FD) CAN messages).
 */
class FDCAN : public CAN
{
 public:
  /**
   * @brief 构造 FDCAN 对象，可指定主题名称和通信域 (Constructs an FDCAN object with
   * optional topic names and domain).
   * @param name_tp 经典 CAN 消息的主题名称 (Topic name for classic CAN messages).
   * @param name_fd_tp FD CAN 消息的主题名称 (Topic name for FD CAN messages).
   * @param domain 可选的通信域 (Optional domain for message communication).
   */
  FDCAN(const char *name_tp = "can", const char *name_fd_tp = "fdcan",
        Topic::Domain *domain = nullptr)
      : CAN(name_tp, domain), fd_tp_(name_fd_tp, sizeof(FDPack), domain, false, false)
  {
  }

  /**
   * @brief FD CAN 消息结构 (Structure representing an FD CAN message).
   */
  typedef union
  {
    struct __attribute__((packed))
    {
      uint32_t id;       ///< 消息 ID (Message ID).
      Type type;         ///< 消息类型 (Message type).
      uint8_t len;       ///< 数据长度，最大 64 字节 (Data length, up to 64 bytes).
      uint8_t data[64];  ///< 数据载荷 (Data payload).
    };
    uint8_t raw[70];  ///< 原始字节数组表示 (Raw byte array representation).
  } FDPack;

  using CAN::AddMessage;

  /**
   * @brief 添加 FD CAN 消息到系统 (Adds an FD CAN message to the system).
   * @param pack FD CAN 消息包 (The FD CAN message packet).
   * @return 操作结果 (ErrorCode indicating success or failure).
   */
  virtual ErrorCode AddMessage(const FDPack &pack) = 0;

  Topic fd_tp_;  ///< FD CAN 消息的主题 (Topic for FD CAN messages).
};

}  // namespace LibXR
