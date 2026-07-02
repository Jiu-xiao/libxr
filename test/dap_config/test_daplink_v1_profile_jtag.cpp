/**
 * @file test_daplink_v1_profile_jtag.cpp
 * @brief Compile-time profile check for DAPLink v1 JTAG support.
 */

#include "usb/device/dap/daplink_v1_profile_jtag.hpp"

namespace LibXRDapConfigTest
{
struct SwdProbe;
}

static_assert(LibXR::USB::DapLinkV1BuildConfig::kEnableJtag);
static_assert(LibXR::USB::DapLinkV1Class<LibXRDapConfigTest::SwdProbe>::JTAG_ENABLED);
