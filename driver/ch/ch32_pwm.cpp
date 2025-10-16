#include "ch32_pwm.hpp"

namespace LibXR
{

CH32PWM::CH32PWM(TIM_TypeDef* tim, uint16_t channel, bool active_high, GPIO_TypeDef* gpio,
                 uint16_t pin, uint32_t pin_remap, bool complementary)
    : tim_(tim),
      channel_(channel),
      active_high_(active_high),
      complementary_(complementary),
      gpio_(gpio),
      pin_(pin),
      pin_remap_(pin_remap)
{
}

bool CH32PWM::OnAPB2(TIM_TypeDef* t)
{
  return (t == TIM1)
#if defined(TIM8)
         || (t == TIM8)
#endif
#if defined(TIM9)
         || (t == TIM9)
#endif
#if defined(TIM10)
         || (t == TIM10)
#endif
      ;
}

bool CH32PWM::IsAdvancedTimer(TIM_TypeDef* t)
{
  return (t == TIM1)
#if defined(TIM8)
         || (t == TIM8)
#endif
#if defined(TIM9)
         || (t == TIM9)
#endif
#if defined(TIM10)
         || (t == TIM10)
#endif
      ;
}

uint32_t CH32PWM::GetTimerClockHz(TIM_TypeDef* t)
{
  RCC_ClocksTypeDef c{};
  RCC_GetClocksFreq(&c);

  const bool on_apb2 = OnAPB2(t);
  const uint32_t pclk = on_apb2 ? c.PCLK2_Frequency : c.PCLK1_Frequency;
  const uint32_t hclk = c.HCLK_Frequency;

  if (pclk == 0u || hclk == 0u) return 0u;

  // APB 预分频 = HCLK / PCLK。=1 表示不分频；>1 表示分频，此时 TIMxCLK = 2 * PCLKx。
  const uint32_t apb_div = hclk / pclk;  // 1/2/4/8/16（取整足够）
  const uint32_t timclk = (apb_div > 1u) ? (pclk * 2u) : pclk;

  return timclk;
}

void CH32PWM::EnableGPIOClock(GPIO_TypeDef* gpio)
{
  if (gpio == GPIOA)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
  else if (gpio == GPIOB)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
  else if (gpio == GPIOC)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
  else if (gpio == GPIOD)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
#if defined(GPIOE)
  else if (gpio == GPIOE)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);
#endif
}

void CH32PWM::EnableTIMClock(TIM_TypeDef* tim)
{
  if (tim == TIM1) RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
#if defined(TIM8) && defined(RCC_APB2Periph_TIM8)
  else if (tim == TIM8)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM8, ENABLE);
#endif
#if defined(TIM9) && defined(RCC_APB2Periph_TIM9)
  else if (tim == TIM9)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM9, ENABLE);
#endif
#if defined(TIM10) && defined(RCC_APB2Periph_TIM10)
  else if (tim == TIM10)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM10, ENABLE);
#endif
#if defined(TIM2) && defined(RCC_APB1Periph_TIM2)
  else if (tim == TIM2)
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
#endif
#if defined(TIM3) && defined(RCC_APB1Periph_TIM3)
  else if (tim == TIM3)
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
#endif
#if defined(TIM4) && defined(RCC_APB1Periph_TIM4)
  else if (tim == TIM4)
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
#endif
#if defined(TIM5) && defined(RCC_APB1Periph_TIM5)
  else if (tim == TIM5)
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM5, ENABLE);
#endif
#if defined(TIM6) && defined(RCC_APB1Periph_TIM6)
  else if (tim == TIM6)
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);
#endif
#if defined(TIM7) && defined(RCC_APB1Periph_TIM7)
  else if (tim == TIM7)
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);
#endif
#if defined(TIM12) && defined(RCC_APB1Periph_TIM12)
  else if (tim == TIM12)
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM12, ENABLE);
#endif
#if defined(TIM13) && defined(RCC_APB1Periph_TIM13)
  else if (tim == TIM13)
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM13, ENABLE);
#endif
#if defined(TIM14) && defined(RCC_APB1Periph_TIM14)
  else if (tim == TIM14)
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM14, ENABLE);
#endif
}

void CH32PWM::ConfigureGPIO()
{
  // AFIO 时钟（用于重映射）
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

  // 需要重映射则打开
  if (pin_remap_ != 0u)
  {
    GPIO_PinRemapConfig(pin_remap_, ENABLE);
  }

  // 端口时钟 + 复用推挽输出
  EnableGPIOClock(gpio_);
  GPIO_InitTypeDef io{};
  io.GPIO_Pin = pin_;
  io.GPIO_Speed = GPIO_Speed_50MHz;
  io.GPIO_Mode = GPIO_Mode_AF_PP;
  GPIO_Init(gpio_, &io);
}

// --- PWM 接口 ---

ErrorCode CH32PWM::SetDutyCycle(float value)
{
  if (!tim_) return ErrorCode::ARG_ERR;

  if (value < 0.0f) value = 0.0f;
  if (value > 1.0f) value = 1.0f;

  const uint32_t arr = ReadARR32(tim_);
  const uint32_t pulse = static_cast<uint32_t>((arr + 1u) * value + 0.5f);

  ApplyCompare(pulse);
  return ErrorCode::OK;
}

ErrorCode CH32PWM::SetConfig(Configuration cfg)
{
  if (!tim_) return ErrorCode::ARG_ERR;
  if (cfg.frequency == 0u) return ErrorCode::ARG_ERR;

  // 先完成引脚与（可选）重映射
  ConfigureGPIO();

  // 确保定时器时钟已开
  EnableTIMClock(tim_);

  const uint32_t timclk = GetTimerClockHz(tim_);
  if (timclk == 0u) return ErrorCode::INIT_ERR;

  // 寻找 PSC/ARR（16 位），偏向更小的 PSC 以提升占空比分辨率
  bool found = false;
  uint32_t best_psc = 1, best_arr = 0;

  for (uint32_t psc = 1; psc <= 0xFFFFu; ++psc)
  {
    const uint32_t arr = timclk / (psc * cfg.frequency);
    if (arr == 0u) break;
    if (arr <= 0x10000u)
    {
      best_psc = psc;
      best_arr = arr;  // 写寄存器时用 (arr-1)
      found = true;
      break;
    }
  }
  if (!found || best_arr == 0u) return ErrorCode::INIT_ERR;

  TIM_TimeBaseInitTypeDef tb{};
  tb.TIM_Prescaler = static_cast<uint16_t>(best_psc - 1u);
  tb.TIM_CounterMode = TIM_CounterMode_Up;
  tb.TIM_Period = static_cast<uint16_t>(best_arr - 1u);
  tb.TIM_ClockDivision = TIM_CKD_DIV1;
  tb.TIM_RepetitionCounter = 0;
  TIM_TimeBaseInit(tim_, &tb);

  // 开启 ARR 预装载
  TIM_ARRPreloadConfig(tim_, ENABLE);

  // 配置通道为 PWM1，高电平有效；初始脉宽 0
  OcInitForChannel(0);

  // 通过 UG 把预装载推入影子寄存器
  TIM_GenerateEvent(tim_, TIM_EventSource_Update);

  // 高级定时器需开启主输出（MOE）
  if (IsAdvancedTimer(tim_))
  {
    TIM_CtrlPWMOutputs(tim_, ENABLE);
  }
  return ErrorCode::OK;
}

ErrorCode CH32PWM::Enable()
{
  if (!tim_) return ErrorCode::ARG_ERR;

  // 先开通道，后启计数器，避免毛刺
  EnableChannel(true);
  if (complementary_ && IsAdvancedTimer(tim_))
  {
    EnableChannelN(true);
  }
  if (IsAdvancedTimer(tim_))
  {
    TIM_CtrlPWMOutputs(tim_, ENABLE);
  }
  TIM_Cmd(tim_, ENABLE);
  return ErrorCode::OK;
}

ErrorCode CH32PWM::Disable()
{
  if (!tim_) return ErrorCode::ARG_ERR;

  if (complementary_ && IsAdvancedTimer(tim_))
  {
    EnableChannelN(false);
  }
  EnableChannel(false);

  return ErrorCode::OK;
}

// --- helpers: compare / channel gates ---

void CH32PWM::ApplyCompare(uint32_t pulse)
{
  const uint16_t ccr = static_cast<uint16_t>(std::min<uint32_t>(pulse, 0xFFFFu));
  switch (channel_)
  {
    case TIM_Channel_1:
      TIM_SetCompare1(tim_, ccr);
      break;
    case TIM_Channel_2:
      TIM_SetCompare2(tim_, ccr);
      break;
    case TIM_Channel_3:
      TIM_SetCompare3(tim_, ccr);
      break;
    case TIM_Channel_4:
      TIM_SetCompare4(tim_, ccr);
      break;
    default:
      break;
  }
}

void CH32PWM::OcInitForChannel(uint32_t pulse)
{
  TIM_OCInitTypeDef oc{};
  oc.TIM_OCMode = TIM_OCMode_PWM1;
  oc.TIM_OutputState = TIM_OutputState_Enable;
  oc.TIM_Pulse = static_cast<uint16_t>(pulse);
  oc.TIM_OCPolarity = active_high_ ? TIM_OCPolarity_High : TIM_OCPolarity_Low;

#if defined(TIM_OCNPolarity_High)
  if (complementary_ && IsAdvancedTimer(tim_))
  {
    oc.TIM_OutputNState = TIM_OutputNState_Enable;
    oc.TIM_OCNPolarity = active_high_ ? TIM_OCNPolarity_High : TIM_OCNPolarity_Low;
  }
#endif

  switch (channel_)
  {
    case TIM_Channel_1:
      TIM_OC1Init(tim_, &oc);
      TIM_OC1PreloadConfig(tim_, TIM_OCPreload_Enable);
      break;
    case TIM_Channel_2:
      TIM_OC2Init(tim_, &oc);
      TIM_OC2PreloadConfig(tim_, TIM_OCPreload_Enable);
      break;
    case TIM_Channel_3:
      TIM_OC3Init(tim_, &oc);
      TIM_OC3PreloadConfig(tim_, TIM_OCPreload_Enable);
      break;
    case TIM_Channel_4:
      TIM_OC4Init(tim_, &oc);
      TIM_OC4PreloadConfig(tim_, TIM_OCPreload_Enable);
      break;
    default:
      break;
  }
}

void CH32PWM::EnableChannel(bool en)
{
#if defined(TIM_CCx_Enable)
  switch (channel_)
  {
    case TIM_Channel_1:
      TIM_CCxCmd(tim_, TIM_Channel_1, en ? TIM_CCx_Enable : TIM_CCx_Disable);
      break;
    case TIM_Channel_2:
      TIM_CCxCmd(tim_, TIM_Channel_2, en ? TIM_CCx_Enable : TIM_CCx_Disable);
      break;
    case TIM_Channel_3:
      TIM_CCxCmd(tim_, TIM_Channel_3, en ? TIM_CCx_Enable : TIM_CCx_Disable);
      break;
    case TIM_Channel_4:
      TIM_CCxCmd(tim_, TIM_Channel_4, en ? TIM_CCx_Enable : TIM_CCx_Disable);
      break;
    default:
      break;
  }
#else
  (void)en;
#endif
}

void CH32PWM::EnableChannelN(bool en)
{
  if (!IsAdvancedTimer(tim_)) return;
  switch (channel_)
  {
    case TIM_Channel_1:
      TIM_CCxNCmd(tim_, TIM_Channel_1, en ? TIM_CCxN_Enable : TIM_CCxN_Disable);
      break;
    case TIM_Channel_2:
      TIM_CCxNCmd(tim_, TIM_Channel_2, en ? TIM_CCxN_Enable : TIM_CCxN_Disable);
      break;
    case TIM_Channel_3:
      TIM_CCxNCmd(tim_, TIM_Channel_3, en ? TIM_CCxN_Enable : TIM_CCxN_Disable);
      break;
    default:
      break;  // CH4 无 N
  }
}

}  // namespace LibXR
