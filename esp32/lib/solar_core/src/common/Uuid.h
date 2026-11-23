#pragma once

#include <cstdio>
#include <string>

#include "common/Platform.h"

namespace solar {

/** A random identifier in UUID layout, used for OCPP message ids and id tags. */
inline std::string newUuid(Platform& platform) {
    uint32_t a = platform.random32(), b = platform.random32(), c = platform.random32(), d = platform.random32();
    char text[37];
    std::snprintf(text, sizeof text, "%08x-%04x-%04x-%04x-%04x%08x", (unsigned)a, (unsigned)(b >> 16),
                  (unsigned)((b & 0x0fff) | 0x4000), (unsigned)(((c >> 16) & 0x3fff) | 0x8000), (unsigned)(c & 0xffff),
                  (unsigned)d);
    return text;
}

}  // namespace solar
