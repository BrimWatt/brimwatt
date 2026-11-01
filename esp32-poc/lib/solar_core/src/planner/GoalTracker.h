#pragma once

#include <cstdint>

#include "planner/GoalPlanner.h"

namespace solar {

/**
 * Notices when a departure goal's deadline has just passed, once, so the controller can record whether the goal was met.
 * Pure: the caller supplies the time and the goal. A deadline is judged only if it passed a moment ago (a poll or two),
 * never one that passed while the board was off or its clock was wrong; changing the goal or its time starts over without
 * judging anything. Same rules as the Java app's GoalTracker.
 */
class GoalTracker {
public:
    /** A deadline older than this when first noticed is not judged. */
    static constexpr int64_t kLateSeconds = 15 * 60;

    /** Returns the deadline (epoch seconds) to judge now, or -1 if there is none. */
    int64_t due(int64_t nowSec, int utcOffsetSeconds, int goalMinutes, int goalKwh) {
        if (goalKwh <= 0) {
            evaluated_ = 0; minutes_ = -1; kwh_ = -1;
            return -1;
        }
        const int64_t last = goal::previousDeadline(nowSec, utcOffsetSeconds, goalMinutes);
        if (evaluated_ == 0 || goalMinutes != minutes_ || goalKwh != kwh_) {
            minutes_ = goalMinutes; kwh_ = goalKwh; evaluated_ = last;
            return -1;
        }
        if (last <= evaluated_) return -1;
        evaluated_ = last;
        return nowSec - last <= kLateSeconds ? last : -1;
    }

private:
    int64_t evaluated_ = 0;
    int minutes_ = -1;
    int kwh_ = -1;
};

}  // namespace solar
