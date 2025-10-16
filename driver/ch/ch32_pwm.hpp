#pragma once

#include "pwm.hpp"

#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

namespace LibXR
{
class CH32PWM : public PWM
{
 public:
  CH32PWM(TIM_TypeDef* tim, uint16_t channel, bool active_high, GPIO_TypeDef* gpio,
          uint16_t pin, uint32_t pin_remap, bool complementary = false);

  ErrorCode SetDutyCycle(float value) override;
  ErrorCode SetConfig(Configuration config) override;
  ErrorCode Enable() override;
  ErrorCode Disable() override;

 private:
  TIM_TypeDef* tim_;
  uint16_t channel_;
  bool active_high_;
  bool complementary_;

  GPIO_TypeDef* gpio_;
  uint16_t pin_;
  uint32_t pin_remap_;

  static bool IsAdvancedTimer(TIM_TypeDef* t);
  static bool OnAPB2(TIM_TypeDef* t);
  static uint32_t GetTimerClockHz(TIM_TypeDef* t);

  static inline uint32_t ReadARR32(TIM_TypeDef* t) { return t->ATRLR; }

  void ApplyCompare(uint32_t pulse);
  void OcInitForChannel(uint32_t pulse);
  void EnableChannel(bool en);
  void EnableChannelN(bool en);

  // 时钟/引脚配置
  static void EnableGPIOClock(GPIO_TypeDef* gpio);
  static void EnableTIMClock(TIM_TypeDef* tim);
  void ConfigureGPIO();
};

}  // namespace LibXR
