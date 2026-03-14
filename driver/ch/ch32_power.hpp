#pragma once

#include "power.hpp"

#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

namespace LibXR
{

class CH32PowerManager : public PowerManager
{
 public:
  CH32PowerManager() = default;

  void Reset() override;

  void Shutdown() override;
};

}  // namespace LibXR
