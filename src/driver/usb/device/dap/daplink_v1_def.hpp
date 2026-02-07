#pragma once

#include <cstdint>

#include "daplink_v2_def.hpp"

namespace LibXR::USB::DapLinkV1Def
{

using namespace DapLinkV2Def;

static constexpr std::uint16_t MAX_REQUEST_SIZE = 64u;
static constexpr std::uint16_t MAX_RESPONSE_SIZE = 64u;

}  // namespace LibXR::USB::DapLinkV1Def
