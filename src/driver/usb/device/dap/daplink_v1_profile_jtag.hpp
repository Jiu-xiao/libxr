#pragma once

#ifndef LIBXR_USB_DAPLINK_V1_BUILD_CONFIG_DEFINED
#define LIBXR_USB_DAPLINK_V1_BUILD_CONFIG_DEFINED
namespace LibXR::USB::DapLinkV1BuildConfig
{
static constexpr bool kEnableJtag = true;
}
#else
static_assert(LibXR::USB::DapLinkV1BuildConfig::kEnableJtag,
              "Conflicting DAPLink v1 build profile");
#endif

#include "daplink_v1.hpp"
