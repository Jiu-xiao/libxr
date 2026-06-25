#pragma once

#include <cstdint>

namespace LibXR
{

enum class ErrorCode : uint8_t
{
  OK = 0,
  FAILED,
  ARG_ERR,
  STATE_ERR,
  NOT_SUPPORT
};

template <typename... Args>
class Callback
{
 public:
  Callback() = default;

  template <typename Context>
  static Callback Create(void (*fn)(bool, Context*), Context* context)
  {
    Callback cb;
    cb.fn_ = reinterpret_cast<void (*)(bool, void*)>(fn);
    cb.context_ = context;
    return cb;
  }

  void Run(bool in_isr) const
  {
    if (fn_ != nullptr)
    {
      fn_(in_isr, context_);
    }
  }

 private:
  void (*fn_)(bool, void*) = nullptr;
  void* context_ = nullptr;
};

class GPIO
{
 public:
  enum class Direction : uint8_t
  {
    INPUT,
    OUTPUT_PUSH_PULL,
    OUTPUT_OPEN_DRAIN,
    FALL_INTERRUPT,
    RISING_INTERRUPT,
    FALL_RISING_INTERRUPT
  };

  enum class Pull : uint8_t
  {
    NONE,
    UP,
    DOWN
  };

  struct Configuration
  {
    Direction direction;
    Pull pull;
  };

  using Callback = LibXR::Callback<>;

  GPIO() = default;
  virtual ~GPIO() = default;

  virtual bool Read() = 0;
  virtual void Write(bool value) = 0;
  virtual ErrorCode EnableInterrupt() = 0;
  virtual ErrorCode DisableInterrupt() = 0;
  virtual ErrorCode SetConfig(Configuration config) = 0;

  ErrorCode RegisterCallback(Callback callback)
  {
    callback_ = callback;
    return ErrorCode::OK;
  }

  Callback callback_;
};

}  // namespace LibXR
