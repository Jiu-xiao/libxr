/**
 * @file test_daplink_v2_profile_swd.cpp
 * @brief Compile-time profile check for DAPLink v2 SWD-only support.
 */

#include "usb/device/dap/daplink_v2_profile_swd.hpp"

namespace LibXRDapConfigTest
{
struct SwdProbe;
}

static_assert(!LibXR::USB::DapLinkV2BuildConfig::kEnableJtag);
static_assert(!LibXR::USB::DapLinkV2Class<LibXRDapConfigTest::SwdProbe>::JTAG_ENABLED);
