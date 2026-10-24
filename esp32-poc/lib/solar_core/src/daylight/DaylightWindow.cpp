#include "daylight/DaylightWindow.h"

#include <cstdio>

#include "common/CivilDate.h"
#include "daylight/SunCalculator.h"

namespace solar {

int64_t DaylightWindow::localDay(int64_t epochSeconds) const {
    return floorDiv(epochSeconds + platform_.utcOffsetSeconds(epochSeconds), 86400);
}

std::optional<DaylightWindow::Span> DaylightWindow::spanFor(int64_t epochDay) const {
    SunTimes sun = calculateSun(epochDay, latitude_, longitude_,
                                [this](int64_t t) { return platform_.utcOffsetSeconds(t); });
    if (sun.kind == SunTimes::Kind::AlwaysDown) return std::nullopt;
    if (sun.kind == SunTimes::Kind::AlwaysUp) {
        // Local midnight to local midnight, using the offset around local noon of that day.
        int64_t offset = platform_.utcOffsetSeconds(sun.noon);
        return Span{epochDay * 86400 - offset, (epochDay + 1) * 86400 - offset};
    }
    int64_t start = sun.sunrise + startAfterSunrise_;
    int64_t end = sun.sunset - endBeforeSunset_;
    if (end <= start) return std::nullopt;
    return Span{start, end};
}

bool DaylightWindow::isOpen(int64_t epochSeconds) const {
    auto span = spanFor(localDay(epochSeconds));
    return span && epochSeconds >= span->start && epochSeconds < span->end;
}

bool DaylightWindow::windowFor(int64_t epochSeconds, int64_t& start, int64_t& end) const {
    auto span = spanFor(localDay(epochSeconds));
    if (!span) return false;
    start = span->start;
    end = span->end;
    return true;
}

std::string DaylightWindow::describe(int64_t epochSeconds) const {
    auto span = spanFor(localDay(epochSeconds));
    if (!span) return "no daylight window today";
    auto clock = [this](int64_t t) {
        int64_t local = t + platform_.utcOffsetSeconds(t);
        int64_t secondsOfDay = local - floorDiv(local, 86400) * 86400;
        char text[8];
        std::snprintf(text, sizeof text, "%02d:%02d", static_cast<int>(secondsOfDay / 3600),
                      static_cast<int>(secondsOfDay % 3600 / 60));
        return std::string(text);
    };
    return clock(span->start) + " to " + clock(span->end);
}

}  // namespace solar
