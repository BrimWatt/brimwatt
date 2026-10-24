#pragma once

#include <cstdint>
#include <functional>

namespace solar {

/**
 * Sunrise and sunset from a date and a position, using the NOAA solar calculation (accurate to about a minute
 * away from the poles). Needs no network access.
 */
struct SunTimes {
    enum class Kind {
        Normal,       // the sun rises and sets on this date
        AlwaysUp,     // polar day
        AlwaysDown    // polar night
    };

    Kind kind = Kind::Normal;
    int64_t sunrise = 0;   // epoch seconds (meaningless unless Normal)
    int64_t noon = 0;
    int64_t sunset = 0;
};

/**
 * @param epochDay  the local calendar date as days since 1970-01-01
 * @param latitude  degrees, north positive
 * @param longitude degrees, east positive (so the western US is negative)
 * @param utcOffset local offset from UTC in seconds at a given instant; used only to keep solar noon on the
 *                  right local date far from Greenwich
 */
SunTimes calculateSun(int64_t epochDay, double latitude, double longitude,
                      const std::function<int(int64_t epochSeconds)>& utcOffset);

}  // namespace solar
