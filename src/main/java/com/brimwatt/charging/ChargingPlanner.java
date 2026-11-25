package com.brimwatt.charging;

/**
 * Decides the charging current from a stream of "available amps" readings, smoothing out
 * short-lived swings so the charger is not reconfigured on every poll.
 *
 * <p>Available amps = what the charger is already drawing + the surplus still being exported.
 * Using the draw as well avoids a feedback loop: the charger's own load shrinks the exported
 * surplus, so surplus alone would make the limit oscillate even under constant sun.
 *
 * <p>Not thread-safe; call from one thread (the poller).
 */
public class ChargingPlanner {

    /**
     * @param amps        target current, 0 meaning "not charging"
     * @param waitSeconds seconds until a start (while confirming, or the pause after a stop) or a stop (during a dip) can
     *                    happen if nothing changes; -1 when no timer is running
     */
    public record Decision(int amps, double smoothedAmps, String reason, int waitSeconds) {
        public Decision(int amps, double smoothedAmps, String reason) { this(amps, smoothedAmps, reason, -1); }
    }

    /** Lets a smoothed value that is converging on a whole number (19.9 -> 20) round to it. */
    private static final double ROUNDING_TOLERANCE_AMPS = 0.25;

    private ChargingPolicy policy;
    private final int ceilingAmps;   // the policy's own maximum; the installation's limit can only lower it

    private Double slow;
    private Double fast;
    private long lastTick;
    private Integer applied;          // null while not charging
    private long lastLimitChange;
    private long stoppedAt;
    private long aboveSince = -1;
    private long belowSince = -1;     // when the available current first fell short of the minimum, while charging

    public ChargingPlanner(ChargingPolicy policy) {
        this.policy = policy;
        this.ceilingAmps = policy.maxAmps();
        reset(0, null);
    }

    /** Applies the installation's maximum current (the setting); the planner never asks for more than this or its own ceiling. */
    public void setMaxAmps(int maxAmps) {
        int effective = Math.max(policy.minAmps(), Math.min(ceilingAmps, maxAmps));
        if (effective != policy.maxAmps()) policy = policy.withMaxAmps(effective);
    }

    /**
     * Forgets history, e.g. when the mode changes or the charger reconnects.
     *
     * @param runningAmps the limit currently in effect if a charge is in progress, otherwise null
     */
    public void reset(long nowMillis, Integer runningAmps) {
        slow = null;
        fast = null;
        applied = runningAmps;
        // A charge that is already running (arriving from FAST mode, or found after a restart) has
        // been going for an unknown time, so it may be adjusted or stopped without waiting.
        boolean alreadyRunning = runningAmps != null;
        lastLimitChange = alreadyRunning ? nowMillis - policy.minIncreaseIntervalMillis() : nowMillis;
        stoppedAt = nowMillis - policy.minOffMillis();
        aboveSince = -1;
        belowSince = -1;
    }

    /**
     * Starts the averages from the next reading without touching the start timers. Used when an
     * input to the reading (the grid allowance) changed, so the change shows up now instead of after
     * several minutes of smoothing.
     */
    public void reseedAverages() {
        slow = null;
        fast = null;
    }

    /**
     * Brings the planner in line with a charge that is already running but that it did not start
     * (after a restart, or when the charger was charging before the app connected). Without this
     * the planner believes nothing is charging and would stop a car that should keep going.
     *
     * @param runningAmps the limit in effect on a charging charger, or null if none is charging
     */
    public void adoptRunning(long nowMillis, Integer runningAmps) {
        if (runningAmps == null || applied != null) return;
        applied = clamp(runningAmps);
        // We do not know how long it has been running, so allow adjusting and stopping right away.
        lastLimitChange = nowMillis - policy.minIncreaseIntervalMillis();
        aboveSince = -1;
        belowSince = -1;
    }

    public Decision next(long nowMillis, double availableAmps) { return next(nowMillis, availableAmps, 0); }

    /**
     * @param topUpAmps how much the grid may add, in amps; used only by a policy that tops up to a whole amp
     *                  (Max solar), where {@code availableAmps} is the solar output alone
     */
    public Decision next(long nowMillis, double availableAmps, double topUpAmps) {
        if (slow == null) {
            slow = availableAmps;
            fast = availableAmps;
        } else {
            double elapsedSeconds = (nowMillis - lastTick) / 1000.0;
            slow += weight(elapsedSeconds, policy.slowAverageSeconds()) * (availableAmps - slow);
            fast += weight(elapsedSeconds, policy.fastAverageSeconds()) * (availableAmps - fast);
        }
        lastTick = nowMillis;
        // Min of the two: falls with the fast average, rises only with the slow one.
        double smoothed = Math.min(slow, fast);
        int candidate = policy.topUpToWholeAmp() ? topUpCandidate(smoothed, topUpAmps)
                : (int) Math.floor(smoothed + ROUNDING_TOLERANCE_AMPS);

        return applied == null ? whileIdle(nowMillis, candidate, smoothed) : whileCharging(nowMillis, candidate, smoothed);
    }

    /**
     * The whole amp the car should run at when the grid may fill a gap of up to {@code topUpAmps}: the next amp up
     * (or the charger's minimum, if the sun is below it) when the gap fits, otherwise the amp below. A result under
     * the minimum means there is not enough sun even with the top-up.
     */
    private int topUpCandidate(double solarAmps, double topUpAmps) {
        int target = (int) Math.ceil(Math.max(solarAmps, policy.minAmps()));
        if (target - solarAmps <= topUpAmps + ROUNDING_TOLERANCE_AMPS) return target;
        return (int) Math.floor(solarAmps + ROUNDING_TOLERANCE_AMPS);
    }

    /** Share of the gap to the new reading that a moving average with this time constant closes. */
    private static double weight(double elapsedSeconds, double timeConstantSeconds) {
        return 1 - Math.exp(-Math.max(elapsedSeconds, 0) / timeConstantSeconds);
    }

    private Decision whileIdle(long now, int candidate, double smoothed) {
        if (candidate < policy.startAmps()) {
            aboveSince = -1;
            return new Decision(0, smoothed, "waiting for surplus", secondsLeft(policy.minOffMillis() - (now - stoppedAt)));
        }
        if (aboveSince < 0) aboveSince = now;

        boolean sustained = now - aboveSince >= policy.startSustainMillis();
        boolean offLongEnough = now - stoppedAt >= policy.minOffMillis();
        if (!sustained || !offLongEnough) {
            long left = Math.max(policy.startSustainMillis() - (now - aboveSince), policy.minOffMillis() - (now - stoppedAt));
            return new Decision(0, smoothed, "surplus present, confirming it lasts", secondsLeft(left));
        }
        applied = clamp(candidate);
        lastLimitChange = now;
        aboveSince = -1;
        belowSince = -1;
        return new Decision(applied, smoothed, "start");
    }

    private Decision whileCharging(long now, int candidate, double smoothed) {
        if (candidate < policy.minAmps()) {
            // Even the minimum current would import more from the grid than the allowance. Ride out a short
            // dip at the minimum; stop once the shortfall has lasted the stop delay.
            if (belowSince < 0) belowSince = now;
            if (now - belowSince >= policy.stopSustainMillis()) {
                applied = null;
                stoppedAt = now;
                aboveSince = -1;
                belowSince = -1;
                return new Decision(0, smoothed, "stop: surplus below the minimum, would import more than the allowance");
            }
            Decision d = adjust(now, policy.minAmps(), smoothed, "dip: at the minimum, waiting before stopping", "dip: holding the minimum");
            return new Decision(d.amps(), d.smoothedAmps(), d.reason(), secondsLeft(policy.stopSustainMillis() - (now - belowSince)));
        }
        belowSince = -1;
        return adjust(now, clamp(candidate), smoothed, "track surplus", "no change");
    }

    private Decision adjust(long now, int desired, double smoothed, String reason, String unchangedReason) {
        int diff = desired - applied;
        // Stepping down reacts to a smaller gap than stepping up, so import stays near the allowance.
        boolean significant = Math.abs(diff) >= (diff < 0 ? policy.decreaseDeadbandAmps() : policy.deadbandAmps());
        long minInterval = diff < 0 ? policy.minDecreaseIntervalMillis() : policy.minIncreaseIntervalMillis();
        boolean intervalOk = now - lastLimitChange >= minInterval;

        if (significant && intervalOk) {
            applied = desired;
            lastLimitChange = now;
            return new Decision(applied, smoothed, reason);
        }
        return new Decision(applied, smoothed, unchangedReason);
    }

    /** Whole seconds left on a timer, or -1 when it has run out (nothing to count down). */
    private static int secondsLeft(long millisLeft) {
        return millisLeft > 0 ? (int) Math.ceil(millisLeft / 1000.0) : -1;
    }

    private int clamp(int amps) {
        return Math.min(Math.max(amps, policy.minAmps()), policy.maxAmps());
    }
}
