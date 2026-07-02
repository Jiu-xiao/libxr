#pragma once

#ifndef LIBXR_USB_DAPLINK_V2_BUILD_CONFIG_DEFINED
#define LIBXR_USB_DAPLINK_V2_BUILD_CONFIG_DEFINED
namespace LibXR::USB::DapLinkV2BuildConfig
{
static constexpr bool kEnableJtag = true;
}
#else
static_assert(LibXR::USB::DapLinkV2BuildConfig::kEnableJtag,
              "Conflicting DAPLink v2 build profile");
#endif

#include "daplink_v2.hpp"
