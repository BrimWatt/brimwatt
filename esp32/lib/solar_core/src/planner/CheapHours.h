#pragma once

#include <cstdint>
#include <functional>

namespace solar {

/**
 * "Cheapest hours": the expensive hours are the peak window from Settings (start hour to end hour, wrapping past midnight);
 * everything else is cheap. With weekends off the window only applies Monday to Friday. Mirrors com.wattsync.charging.CheapHours.
 */
struct CheapHours {
    /** utcOffset gives the local offset in seconds at an instant (daylight saving included). */
    using OffsetFn = std::function<int(int64_t)>;

    static bool expensive(int64_t epoch, int utcOffsetSeconds, int startHour, int endHour, bool weekends) {
        const int64_t local = epoch + utcOffsetSeconds;
        const int64_t day = floorDiv(local, 86400);
        const int weekday = static_cast<int>(((day + 4) % 7 + 7) % 7);   // 0 = Sunday; 1 Jan 1970 was a Thursday
        if (!weekends && (weekday == 0 || weekday == 6)) return false;
        const int hour = static_cast<int>((local - day * 86400) / 3600);
        return startHour < endHour ? hour >= startHour && hour < endHour : hour >= startHour || hour < endHour;
    }

    /** Seconds until the price period changes, or -1 if it does not within a week. */
    static int64_t secondsToChange(int64_t epoch, const OffsetFn& offset, int startHour, int endHour, bool weekends) {
        const bool current = expensive(epoch, offset(epoch), startHour, endHour, weekends);
        const int64_t local = epoch + offset(epoch);
        int64_t step = epoch + (3600 - (((local % 3600) + 3600) % 3600));   // the next local hour boundary
        for (int i = 0; i < 24 * 8; i++, step += 3600) {
            if (expensive(step, offset(step), startHour, endHour, weekends) != current) return step - epoch;
        }
        return -1;
    }

private:
    static int64_t floorDiv(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0); }
};

}  // namespace solar
