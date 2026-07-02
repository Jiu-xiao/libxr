/**
 * @file test_daplink_v1_profile_swd.cpp
 * @brief Compile-time profile check for DAPLink v1 SWD-only support.
 */

#include "usb/device/dap/daplink_v1_profile_swd.hpp"

namespace LibXRDapConfigTest
{
struct SwdProbe;
}

static_assert(!LibXR::USB::DapLinkV1BuildConfig::kEnableJtag);
static_assert(!LibXR::USB::DapLinkV1Class<LibXRDapConfigTest::SwdProbe>::JTAG_ENABLED);
