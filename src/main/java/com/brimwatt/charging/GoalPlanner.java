package com.brimwatt.charging;

/**
 * The departure goal: "charge at least X kWh by a time". Pure arithmetic, no clock or charger, so it is easy to test.
 *
 * <p>The goal only matters when time is running out. While there is plenty of time, Eco or Max solar charge as they
 * normally do (from the sun). When the time left is only just enough to deliver what is still missing at the
 * highest current, the goal takes over and asks for the lowest current that still gets there, grid power included.
 */
public final class GoalPlanner {

    /** Start this long before the last moment that would still work, as a margin for a slow car or a slow charger. */
    /** Energy counts toward a goal from this long before the deadline (a night's charge, not yesterday's). */
    public static final long WINDOW_SECONDS = 16 * 3600;

    static final long MARGIN_SECONDS = 15 * 60;

    private GoalPlanner() {}

    /**
     * @param forcedAmps  0 if the goal does not need to take over now, otherwise the current to ask for
     * @param remainingWh what is still missing (0 once the goal is met)
     * @param met         the goal has been reached
     * @param unreachable even the highest current cannot deliver it in the time left
     */
    public record Decision(int forcedAmps, double remainingWh, boolean met, boolean unreachable) {}

    /** The next time of day (minutes after local midnight) as an epoch second, today if it is still ahead, otherwise tomorrow. */
    public static long nextDeadline(long nowEpoch, int utcOffsetSeconds, int goalMinutes) {
        long localNow = nowEpoch + utcOffsetSeconds;
        long secondOfDay = ((localNow % 86400) + 86400) % 86400;
        long delta = goalMinutes * 60L - secondOfDay;
        if (delta <= 0) delta += 86400;
        return nowEpoch + delta;
    }

    /** The most recent deadline that has already passed (or is right now): the one before {@link #nextDeadline}. */
    public static long previousDeadline(long nowEpoch, int utcOffsetSeconds, int goalMinutes) {
        return nextDeadline(nowEpoch, utcOffsetSeconds, goalMinutes) - 86400;
    }

    public static Decision decide(double goalWh, double doneWh, long secondsLeft, int minAmps, int maxAmps, double volts) {
        double remaining = Math.max(0, goalWh - doneWh);
        if (goalWh <= 0 || remaining <= 0) return new Decision(0, 0, goalWh > 0, false);
        double hoursLeft = Math.max(secondsLeft, 1) / 3600.0;
        double secondsNeededAtMax = remaining / (maxAmps * volts) * 3600.0;
        boolean unreachable = secondsNeededAtMax > secondsLeft;
        if (secondsLeft > secondsNeededAtMax + MARGIN_SECONDS) return new Decision(0, remaining, false, false);
        int amps = (int) Math.ceil(remaining / hoursLeft / volts);
        amps = Math.max(minAmps, Math.min(maxAmps, unreachable ? maxAmps : amps));
        return new Decision(amps, remaining, false, unreachable);
    }
}
