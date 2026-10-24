#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace solar {

/**
 * The departure goal: "charge at least X kWh by a time". Pure arithmetic, no clock or charger.
 * Same rules as the Java app's GoalPlanner: while there is plenty of time the solar rules run as usual; when the time
 * left is only just enough, the goal asks for the lowest current that still gets there, grid power included.
 */
namespace goal {

/** Energy counts toward a goal from this long before the deadline (a night's charge, not yesterday's). */
constexpr int64_t kWindowSeconds = 16 * 3600;
/** Start this long before the last moment that would still work, as a margin for a slow car or a slow charger. */
constexpr int64_t kMarginSeconds = 15 * 60;

struct Decision {
    int forcedAmps = 0;        // 0 if the goal does not need to take over now
    double remainingWh = 0;    // what is still missing
    bool met = false;          // the goal has been reached
    bool unreachable = false;  // even the highest current cannot deliver it in the time left
};

/** The next time of day (minutes after local midnight) as an epoch second: today if still ahead, otherwise tomorrow. */
inline int64_t nextDeadline(int64_t nowEpoch, int utcOffsetSeconds, int goalMinutes) {
    int64_t localNow = nowEpoch + utcOffsetSeconds;
    int64_t secondOfDay = ((localNow % 86400) + 86400) % 86400;
    int64_t delta = goalMinutes * 60LL - secondOfDay;
    if (delta <= 0) delta += 86400;
    return nowEpoch + delta;
}

inline Decision decide(double goalWh, double doneWh, int64_t secondsLeft, int minAmps, int maxAmps, double volts) {
    Decision d;
    d.remainingWh = std::max(0.0, goalWh - doneWh);
    if (goalWh <= 0 || d.remainingWh <= 0) {
        d.remainingWh = 0;
        d.met = goalWh > 0;
        return d;
    }
    double hoursLeft = std::max<int64_t>(secondsLeft, 1) / 3600.0;
    double secondsNeededAtMax = d.remainingWh / (maxAmps * volts) * 3600.0;
    d.unreachable = secondsNeededAtMax > static_cast<double>(secondsLeft);
    if (static_cast<double>(secondsLeft) > secondsNeededAtMax + kMarginSeconds) {
        d.unreachable = false;
        return d;
    }
    int amps = static_cast<int>(std::ceil(d.remainingWh / hoursLeft / volts));
    d.forcedAmps = std::max(minAmps, std::min(maxAmps, d.unreachable ? maxAmps : amps));
    return d;
}

}  // namespace goal
}  // namespace solar
