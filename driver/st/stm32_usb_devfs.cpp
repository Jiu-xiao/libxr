#include "stm32_usb_devfs.hpp"

#if defined(HAL_PCD_MODULE_ENABLED)

using namespace LibXR;

void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd) {}
void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
  if (STM32USBDeviceFS::self_)
  {
    STM32USBDeviceFS::self_->OnSetupPacket(
        true, reinterpret_cast<const USB::SetupPacket *>(hpcd->Setup));
  }
}
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd) {}
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd) {}
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd) {}
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd) {}
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd) {}

#endif
