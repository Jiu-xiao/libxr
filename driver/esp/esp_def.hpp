#pragma once

// Shared ESP driver definitions/header hygiene entry point.
// Keep ESP-IDF/FreeRTOS integration quirks localized here so the
// rest of the ESP driver headers can include one clean common header.
#include "sdkconfig.h"
#include <FreeRTOS.h>

// Xtensa/FreeRTOS headers may expose INTERRUPT macro and collide with
// Endpoint::Type::INTERRUPT in generic USB core headers.
#if defined(INTERRUPT)
#undef INTERRUPT
#endif
