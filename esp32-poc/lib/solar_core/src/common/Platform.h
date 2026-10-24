#pragma once

#include <cstdint>
#include <string>

namespace solar {

/**
 * Everything the portable code needs from the machine it runs on. The firmware supplies the real clock
 * and random numbers; the unit tests supply fakes, so the logic can be tested on a laptop.
 */
class Platform {
public:
    virtual ~Platform() = default;

    /** Milliseconds since boot. Only ever used for differences. */
    virtual uint64_t nowMillis() const = 0;

    /** Wall-clock seconds since 1970 (valid once the time has been synced). */
    virtual int64_t epochSeconds() const = 0;

    /** Current UTC time in the OCPP format, for example 2026-10-14T22:06:49.123Z. */
    virtual std::string utcIso() const = 0;

    virtual uint32_t random32() = 0;

    /** Offset of local time from UTC at the given instant, daylight saving included (-25200 for PDT). */
    virtual int utcOffsetSeconds(int64_t epochSeconds) const = 0;
};

}  // namespace solar
