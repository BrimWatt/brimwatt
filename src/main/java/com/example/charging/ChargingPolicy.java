package com.example.charging;

import com.example.config.AppConfig;

import java.util.concurrent.TimeUnit;

/**
 * Tunable behaviour of a solar-following mode. Both modes share one rule: they charge only while the
 * available current (surplus plus the grid allowance) covers {@code minAmps}. When it falls short they run at the
 * minimum for {@code stopSustainMillis} before stopping, so a short dip may import a little over the allowance. They differ in the minimum current and the level needed to start.
 */
public record ChargingPolicy(
        int minAmps,
        int maxAmps,
        int startAmps,
        long startSustainMillis,
        long minOffMillis,
        double slowAverageSeconds,
        double fastAverageSeconds,
        int deadbandAmps,
        int decreaseDeadbandAmps,
        long minIncreaseIntervalMillis,
        long minDecreaseIntervalMillis,
        long stopSustainMillis,
        boolean topUpToWholeAmp) {

    /** Without a stop delay: a shortfall stops the charge at once. */
    public ChargingPolicy(int minAmps, int maxAmps, int startAmps, long startSustainMillis, long minOffMillis,
                          double slowAverageSeconds, double fastAverageSeconds, int deadbandAmps, int decreaseDeadbandAmps,
                          long minIncreaseIntervalMillis, long minDecreaseIntervalMillis) {
        this(minAmps, maxAmps, startAmps, startSustainMillis, minOffMillis, slowAverageSeconds, fastAverageSeconds,
                deadbandAmps, decreaseDeadbandAmps, minIncreaseIntervalMillis, minDecreaseIntervalMillis, 0, false);
    }

    /** Eco-style rule (house load subtracted, allowance added to the available current) with a stop delay. */
    public ChargingPolicy(int minAmps, int maxAmps, int startAmps, long startSustainMillis, long minOffMillis,
                          double slowAverageSeconds, double fastAverageSeconds, int deadbandAmps, int decreaseDeadbandAmps,
                          long minIncreaseIntervalMillis, long minDecreaseIntervalMillis, long stopSustainMillis) {
        this(minAmps, maxAmps, startAmps, startSustainMillis, minOffMillis, slowAverageSeconds, fastAverageSeconds,
                deadbandAmps, decreaseDeadbandAmps, minIncreaseIntervalMillis, minDecreaseIntervalMillis, stopSustainMillis, false);
    }

    /** Solar plus the allowance: minimum is the charger's 6 A. */
    public static ChargingPolicy eco() {
        return new ChargingPolicy(
                AppConfig.MIN_AMPS, AppConfig.MAX_AMPS, AppConfig.ECO_START_AMPS,
                seconds(AppConfig.ECO_START_SUSTAIN_SECONDS), seconds(AppConfig.ECO_MIN_OFF_SECONDS),
                AppConfig.SLOW_AVERAGE_SECONDS, AppConfig.FAST_AVERAGE_SECONDS,
                AppConfig.DEADBAND_AMPS, AppConfig.DECREASE_DEADBAND_AMPS, seconds(AppConfig.MIN_INCREASE_INTERVAL_SECONDS),
                seconds(AppConfig.MIN_DECREASE_INTERVAL_SECONDS), seconds(AppConfig.STOP_SUSTAIN_SECONDS), false);
    }

    /**
     * Max solar (internally MAX_SOLAR): the car takes what the panels make, whatever the house is using. Amps follow the
     * solar output; the grid allowance only tops the car up to the next whole amp (or to the charger's 6 A minimum).
     */
    public static ChargingPolicy maxSolar() {
        return new ChargingPolicy(
                AppConfig.MIN_AMPS, AppConfig.MAX_AMPS, AppConfig.MIN_AMPS,
                seconds(AppConfig.ECO_START_SUSTAIN_SECONDS), seconds(AppConfig.ECO_MIN_OFF_SECONDS),
                AppConfig.SLOW_AVERAGE_SECONDS, AppConfig.FAST_AVERAGE_SECONDS,
                AppConfig.DEADBAND_AMPS, AppConfig.DECREASE_DEADBAND_AMPS, seconds(AppConfig.MIN_INCREASE_INTERVAL_SECONDS),
                seconds(AppConfig.MIN_DECREASE_INTERVAL_SECONDS), seconds(AppConfig.STOP_SUSTAIN_SECONDS), true);
    }

    /** The same policy with a different ceiling for the charging current. */
    public ChargingPolicy withMaxAmps(int newMaxAmps) {
        return new ChargingPolicy(minAmps, newMaxAmps, startAmps, startSustainMillis, minOffMillis, slowAverageSeconds,
                fastAverageSeconds, deadbandAmps, decreaseDeadbandAmps, minIncreaseIntervalMillis, minDecreaseIntervalMillis,
                stopSustainMillis, topUpToWholeAmp);
    }

    private static long seconds(int s) {
        return TimeUnit.SECONDS.toMillis(s);
    }
}
