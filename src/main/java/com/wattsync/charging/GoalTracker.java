package com.wattsync.charging;

/**
 * Notices when a departure goal's deadline has just passed, once, so the app can record whether the goal was met. Pure: the caller
 * supplies the time and the goal. A deadline is judged only if it passed a moment ago (a poll or two), never one that passed while the
 * app was off or the clock was wrong, and changing the goal or its time starts over without judging anything.
 * Mirrored by {@code solar::GoalTracker} on the ESP32.
 */
public final class GoalTracker {

    /** A deadline older than this when first noticed is not judged. */
    public static final long LATE_SECONDS = 15 * 60;

    private long evaluated;
    private int minutes = -1;
    private int kwh = -1;

    /** @return the deadline (epoch seconds) to judge now, or -1 if there is none. */
    public long due(long nowSec, int utcOffsetSeconds, int goalMinutes, int goalKwh) {
        if (goalKwh <= 0) {
            evaluated = 0; minutes = -1; kwh = -1;
            return -1;
        }
        long last = GoalPlanner.previousDeadline(nowSec, utcOffsetSeconds, goalMinutes);
        if (evaluated == 0 || goalMinutes != minutes || goalKwh != kwh) {
            minutes = goalMinutes; kwh = goalKwh; evaluated = last;
            return -1;
        }
        if (last <= evaluated) return -1;
        evaluated = last;
        return nowSec - last <= LATE_SECONDS ? last : -1;
    }
}
