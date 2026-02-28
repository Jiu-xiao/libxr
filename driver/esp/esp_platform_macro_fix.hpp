#pragma once

// Keep this workaround in ESP driver layer only.
// Xtensa headers may expose INTERRUPT macro and collide with
// Endpoint::Type::INTERRUPT in generic USB core headers.
#include <FreeRTOS.h>

#if defined(INTERRUPT)
#undef INTERRUPT
#endif
