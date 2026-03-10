#pragma once

#include <gpiod.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>

#include "gpio.hpp"
#include "logger.hpp"

namespace LibXR
{

/**
 * @enum GPIOEventType
 * @brief GPIO 事件类型。GPIO event type.
 */
enum class GPIOEventType : uint8_t
{
  RISING_EDGE,  ///< 上升沿事件。Rising edge event.
  FALLING_EDGE  ///< 下降沿事件。Falling edge event.
};

/**
 * @struct GPIOEvent
 * @brief GPIO 事件结构体。GPIO event structure.
 */
struct GPIOEvent
{
  int64_t timestamp;   ///< 事件时间戳（纳秒）。Event timestamp in nanoseconds.
  GPIOEventType type;  ///< 事件类型。Event type.
};

/**
 * @class LinuxGPIO
 * @brief 基于 libgpiod v2.x 的 Linux GPIO 实现
 *        Linux GPIO implementation using libgpiod v2.x
 */
class LinuxGPIO : public GPIO
{
 public:
  static constexpr size_t EVENT_BUFFER_CAPACITY = 64;

  /**
   * @brief 构造 LinuxGPIO 对象。Constructs a LinuxGPIO instance.
   * @param chip_path GPIO chip 设备路径（如 "/dev/gpiochip0"）。
   *                  GPIO chip device path (e.g. "/dev/gpiochip0").
   * @param line_offset GPIO line 偏移量。GPIO line offset.
   */
  LinuxGPIO(const std::string& chip_path, unsigned int line_offset)
      : chip_path_(chip_path),
        line_offset_(line_offset),
        chip_(gpiod_chip_open(chip_path.c_str()))
  {
    if (!chip_)
    {
      XR_LOG_ERROR("Failed to open GPIO chip: %s", chip_path.c_str());
      ASSERT(false);
      return;
    }

    settings_ = gpiod_line_settings_new();
    if (!settings_)
    {
      XR_LOG_ERROR("Failed to create GPIO line settings");
      ASSERT(false);
      return;
    }

    line_cfg_ = gpiod_line_config_new();
    if (!line_cfg_)
    {
      XR_LOG_ERROR("Failed to create GPIO line config");
      ASSERT(false);
      return;
    }

    req_cfg_ = gpiod_request_config_new();
    if (!req_cfg_)
    {
      XR_LOG_ERROR("Failed to create GPIO request config");
      ASSERT(false);
      return;
    }

    gpiod_request_config_set_consumer(req_cfg_, "LinuxGPIO");
    gpiod_request_config_set_event_buffer_size(req_cfg_, EVENT_BUFFER_CAPACITY);

    event_buffer_ = gpiod_edge_event_buffer_new(EVENT_BUFFER_CAPACITY);
    if (!event_buffer_)
    {
      XR_LOG_ERROR("Failed to allocate GPIO edge event buffer");
      ASSERT(false);
      return;
    }
  }

  LinuxGPIO(const LinuxGPIO&) = delete;
  LinuxGPIO& operator=(const LinuxGPIO&) = delete;

  /**
   * @brief 读取 GPIO 引脚状态。Reads the GPIO line level.
   * @return true 高电平，false 低电平。True for high level, false for low level.
   */
  bool Read() override
  {
    if (!EnsureConfigured())
    {
      return false;
    }

    enum gpiod_line_value value = gpiod_line_request_get_value(request_, line_offset_);
    if (value == GPIOD_LINE_VALUE_ERROR)
    {
      XR_LOG_ERROR("Failed to read GPIO value: %s", std::strerror(errno));
      return false;
    }

    return value == GPIOD_LINE_VALUE_ACTIVE;
  }

  /**
   * @brief 写入 GPIO 引脚状态。Writes a level to the GPIO line.
   * @param value true 高电平，false 低电平。True for high level, false for low level.
   */
  void Write(bool value) override
  {
    if (!EnsureConfigured())
    {
      return;
    }

    enum gpiod_line_value line_value =
        value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE;

    if (gpiod_line_request_set_value(request_, line_offset_, line_value) < 0)
    {
      XR_LOG_WARN("Failed to write GPIO value: %s", std::strerror(errno));
    }
  }

  /**
   * @brief 使能 GPIO 中断处理状态。Enables GPIO interrupt handling state.
   * @return 操作结果。Operation result.
   */
  ErrorCode EnableInterrupt() override
  {
    if (!has_config_ || !request_)
    {
      return ErrorCode::STATE_ERR;
    }

    if (!IsInterruptDirection(current_config_.direction))
    {
      return ErrorCode::ARG_ERR;
    }

    interrupt_enabled_ = true;
    return ErrorCode::OK;
  }

  /**
   * @brief 禁用 GPIO 中断处理状态。Disables GPIO interrupt handling state.
   * @return 操作结果。Operation result.
   */
  ErrorCode DisableInterrupt() override
  {
    interrupt_enabled_ = false;
    return ErrorCode::OK;
  }

  /**
   * @brief 获取 GPIO request 对应的文件描述符，用于 epoll/poll 注册。
   *        Gets the request file descriptor for epoll/poll registration.
   * @return request 文件描述符。Request file descriptor.
   */
  int GetFd() const
  {
    if (EnsureInterruptReady() != ErrorCode::OK)
    {
      return -1;
    }

    return gpiod_line_request_get_fd(request_);
  }

  /**
   * @brief 非阻塞处理中断事件，排空队列并触发回调。
   *        Handles GPIO interrupt events in non-blocking mode, drains queued events and
   *        dispatches callbacks.
   * @return ErrorCode::OK 表示处理了至少一个事件，ErrorCode::EMPTY 表示无事件。
   *         ErrorCode::OK means at least one event was handled; ErrorCode::EMPTY means no
   *         pending event.
   */
  ErrorCode HandleInterrupt()
  {
    const ErrorCode READY = EnsureInterruptReady();
    if (READY != ErrorCode::OK)
    {
      return READY;
    }

    bool handled = false;
    while (true)
    {
      int ready = gpiod_line_request_wait_edge_events(request_, 0);
      if (ready < 0)
      {
        XR_LOG_ERROR("Failed to poll GPIO edge events: %s", std::strerror(errno));
        return ErrorCode::FAILED;
      }

      if (ready == 0)
      {
        break;
      }

      int read = gpiod_line_request_read_edge_events(request_, event_buffer_,
                                                     EVENT_BUFFER_CAPACITY);
      if (read < 0)
      {
        XR_LOG_ERROR("Failed to read GPIO edge events: %s", std::strerror(errno));
        return ErrorCode::FAILED;
      }

      if (read == 0)
      {
        break;
      }

      handled = true;
      if (!callback_.Empty())
      {
        for (int i = 0; i < read; ++i)
        {
          callback_.Run(false);
        }
      }
    }

    return handled ? ErrorCode::OK : ErrorCode::EMPTY;
  }

  /**
   * @brief 读取单个中断事件。Reads a single interrupt edge event.
   * @param event 输出事件结构体。Output edge event structure.
   * @return 操作结果。Operation result.
   */
  ErrorCode ReadEvent(GPIOEvent& event)
  {
    const ErrorCode READY_STATUS = EnsureInterruptReady();
    if (READY_STATUS != ErrorCode::OK)
    {
      return READY_STATUS;
    }

    int ready = gpiod_line_request_wait_edge_events(request_, 0);
    if (ready < 0)
    {
      XR_LOG_ERROR("Failed to poll GPIO edge events: %s", std::strerror(errno));
      return ErrorCode::FAILED;
    }

    if (ready == 0)
    {
      return ErrorCode::EMPTY;
    }

    int ret = gpiod_line_request_read_edge_events(request_, event_buffer_, 1);

    if (ret < 0)
    {
      XR_LOG_ERROR("Failed to read GPIO edge event: %s", std::strerror(errno));
      return ErrorCode::FAILED;
    }

    if (ret == 0)
    {
      return ErrorCode::EMPTY;
    }

    struct gpiod_edge_event* edge_event =
        gpiod_edge_event_buffer_get_event(event_buffer_, ret - 1);
    if (!edge_event)
    {
      XR_LOG_ERROR("Failed to access GPIO edge event from buffer");
      return ErrorCode::FAILED;
    }

    const uint64_t TIMESTAMP_NS = gpiod_edge_event_get_timestamp_ns(edge_event);
    if (TIMESTAMP_NS > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    {
      XR_LOG_ERROR("GPIO edge event timestamp out of int64 range");
      return ErrorCode::OUT_OF_RANGE;
    }

    event.timestamp = static_cast<int64_t>(TIMESTAMP_NS);
    event.type =
        (gpiod_edge_event_get_event_type(edge_event) == GPIOD_EDGE_EVENT_RISING_EDGE)
            ? GPIOEventType::RISING_EDGE
            : GPIOEventType::FALLING_EDGE;

    return ErrorCode::OK;
  }

  /**
   * @brief 配置 GPIO 引脚参数。Configures GPIO line settings.
   * @param config GPIO 配置。GPIO configuration.
   * @return 操作结果。Operation result.
   */
  ErrorCode SetConfig(Configuration config) override
  {
    if (!settings_ || !line_cfg_ || !req_cfg_ || !chip_)
    {
      return ErrorCode::INIT_ERR;
    }

    interrupt_enabled_ = false;

    gpiod_line_settings_reset(settings_);
    gpiod_line_config_reset(line_cfg_);

    if (ApplyDirection(config.direction) != ErrorCode::OK)
    {
      return ErrorCode::ARG_ERR;
    }

    if (ApplyPull(config.pull) != ErrorCode::OK)
    {
      return ErrorCode::ARG_ERR;
    }

    if (gpiod_line_config_add_line_settings(line_cfg_, &line_offset_, 1, settings_) < 0)
    {
      return ErrorCode::FAILED;
    }

    if (!request_)
    {
      request_ = gpiod_chip_request_lines(chip_, req_cfg_, line_cfg_);
      if (!request_)
      {
        return ErrorCode::FAILED;
      }
    }
    else
    {
      if (gpiod_line_request_reconfigure_lines(request_, line_cfg_) < 0)
      {
        return ErrorCode::FAILED;
      }
    }

    current_config_ = config;
    has_config_ = true;

    return ErrorCode::OK;
  }

 private:
  std::string chip_path_;
  unsigned int line_offset_;
  gpiod_chip* chip_ = nullptr;
  gpiod_edge_event_buffer* event_buffer_ =
      nullptr;  ///< 持久化事件缓冲区。Persistent edge event buffer.
  gpiod_line_settings* settings_ = nullptr;
  gpiod_request_config* req_cfg_ = nullptr;
  gpiod_line_config* line_cfg_ = nullptr;
  gpiod_line_request* request_ = nullptr;
  Configuration current_config_ = {Direction::INPUT, Pull::NONE};
  bool has_config_ = false;
  bool interrupt_enabled_ = false;

  /**
   * @brief 根据 GPIO 方向配置 line settings。
   *        Applies line settings according to GPIO direction.
   * @return 操作结果。Operation result.
   */
  ErrorCode ApplyDirection(Direction direction)
  {
    if (!settings_)
    {
      return ErrorCode::FAILED;
    }

    switch (direction)
    {
      case Direction::INPUT:
        if (gpiod_line_settings_set_direction(settings_, GPIOD_LINE_DIRECTION_INPUT) < 0)
        {
          return ErrorCode::FAILED;
        }
        if (gpiod_line_settings_set_edge_detection(settings_, GPIOD_LINE_EDGE_NONE) < 0)
        {
          return ErrorCode::FAILED;
        }
        break;

      case Direction::OUTPUT_PUSH_PULL:
        if (gpiod_line_settings_set_direction(settings_, GPIOD_LINE_DIRECTION_OUTPUT) < 0)
        {
          return ErrorCode::FAILED;
        }
        if (gpiod_line_settings_set_drive(settings_, GPIOD_LINE_DRIVE_PUSH_PULL) < 0)
        {
          return ErrorCode::FAILED;
        }
        if (gpiod_line_settings_set_edge_detection(settings_, GPIOD_LINE_EDGE_NONE) < 0)
        {
          return ErrorCode::FAILED;
        }
        break;

      case Direction::OUTPUT_OPEN_DRAIN:
        if (gpiod_line_settings_set_direction(settings_, GPIOD_LINE_DIRECTION_OUTPUT) < 0)
        {
          return ErrorCode::FAILED;
        }
        if (gpiod_line_settings_set_drive(settings_, GPIOD_LINE_DRIVE_OPEN_DRAIN) < 0)
        {
          return ErrorCode::FAILED;
        }
        if (gpiod_line_settings_set_edge_detection(settings_, GPIOD_LINE_EDGE_NONE) < 0)
        {
          return ErrorCode::FAILED;
        }
        break;

      case Direction::RISING_INTERRUPT:
        if (gpiod_line_settings_set_direction(settings_, GPIOD_LINE_DIRECTION_INPUT) < 0)
        {
          return ErrorCode::FAILED;
        }
        if (gpiod_line_settings_set_edge_detection(settings_, GPIOD_LINE_EDGE_RISING) < 0)
        {
          return ErrorCode::FAILED;
        }
        break;

      case Direction::FALL_INTERRUPT:
        if (gpiod_line_settings_set_direction(settings_, GPIOD_LINE_DIRECTION_INPUT) < 0)
        {
          return ErrorCode::FAILED;
        }
        if (gpiod_line_settings_set_edge_detection(settings_, GPIOD_LINE_EDGE_FALLING) <
            0)
        {
          return ErrorCode::FAILED;
        }
        break;

      case Direction::FALL_RISING_INTERRUPT:
        if (gpiod_line_settings_set_direction(settings_, GPIOD_LINE_DIRECTION_INPUT) < 0)
        {
          return ErrorCode::FAILED;
        }
        if (gpiod_line_settings_set_edge_detection(settings_, GPIOD_LINE_EDGE_BOTH) < 0)
        {
          return ErrorCode::FAILED;
        }
        break;
    }

    return ErrorCode::OK;
  }

  /**
   * @brief 根据 GPIO 上拉/下拉配置 line settings。
   *        Applies line settings according to GPIO pull mode.
   * @return 操作结果。Operation result.
   */
  ErrorCode ApplyPull(Pull pull)
  {
    if (!settings_)
    {
      return ErrorCode::FAILED;
    }

    int ret = 0;
    switch (pull)
    {
      case Pull::NONE:
        ret = gpiod_line_settings_set_bias(settings_, GPIOD_LINE_BIAS_DISABLED);
        break;
      case Pull::UP:
        ret = gpiod_line_settings_set_bias(settings_, GPIOD_LINE_BIAS_PULL_UP);
        break;
      case Pull::DOWN:
        ret = gpiod_line_settings_set_bias(settings_, GPIOD_LINE_BIAS_PULL_DOWN);
        break;
    }

    return ret < 0 ? ErrorCode::FAILED : ErrorCode::OK;
  }

  /**
   * @brief 判断 direction 是否为中断方向。Checks whether direction is interrupt mode.
   * @return true 表示中断方向。True if direction is interrupt mode.
   */
  static bool IsInterruptDirection(Direction direction)
  {
    return direction == Direction::RISING_INTERRUPT ||
           direction == Direction::FALL_INTERRUPT ||
           direction == Direction::FALL_RISING_INTERRUPT;
  }

  /**
   * @brief 确保 GPIO 已配置。Ensures GPIO has been configured.
   */
  bool EnsureConfigured() const
  {
    if (!has_config_ || !request_)
    {
      XR_LOG_ERROR("GPIO is not configured");
      ASSERT(false);
      return false;
    }

    return true;
  }

  /**
   * @brief 确保中断路径已启用。Ensures interrupt path is enabled and ready.
   */
  ErrorCode EnsureInterruptReady() const
  {
    if (!EnsureConfigured())
    {
      return ErrorCode::STATE_ERR;
    }

    if (!IsInterruptDirection(current_config_.direction))
    {
      XR_LOG_ERROR("GPIO is not configured for interrupt mode");
      ASSERT(false);
      return ErrorCode::ARG_ERR;
    }

    if (!interrupt_enabled_)
    {
      XR_LOG_ERROR("GPIO interrupt is not enabled");
      return ErrorCode::STATE_ERR;
    }

    return ErrorCode::OK;
  }
};

}  // namespace LibXR
