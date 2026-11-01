#pragma once

#include <optional>

#include "common/Platform.h"
#include "daylight/DaylightSchedule.h"

namespace solar {

/** The part of the day between a little after sunrise and a little before sunset. */
class DaylightWindow : public DaylightSchedule {
public:
    struct Span {
        int64_t start;   // epoch seconds
        int64_t end;
    };

    /** The platform supplies the local time zone offset (daylight saving included). */
    DaylightWindow(const Platform& platform, double latitude, double longitude, int startAfterSunriseSeconds,
                   int endBeforeSunsetSeconds)
        : platform_(platform),
          latitude_(latitude),
          longitude_(longitude),
          startAfterSunrise_(startAfterSunriseSeconds),
          endBeforeSunset_(endBeforeSunsetSeconds) {}

    /** Empty if the window never opens on this date (polar night, or a day too short for the margins). */
    std::optional<Span> spanFor(int64_t epochDay) const;

    /** Where the sun is measured from. Set once at start-up, from the saved site. */
    void setLocation(double latitude, double longitude) { latitude_ = latitude; longitude_ = longitude; }

    void setMargins(int startAfterSunriseSeconds, int endBeforeSunsetSeconds) override {
        startAfterSunrise_ = startAfterSunriseSeconds;
        endBeforeSunset_ = endBeforeSunsetSeconds;
    }

    bool isOpen(int64_t epochSeconds) const override;
    bool windowFor(int64_t epochSeconds, int64_t& start, int64_t& end) const override;
    std::string describe(int64_t epochSeconds) const override;

private:
    int64_t localDay(int64_t epochSeconds) const;

    const Platform& platform_;
    double latitude_;
    double longitude_;
    int startAfterSunrise_;
    int endBeforeSunset_;
};

}  // namespace solar
