#pragma once

#include <cstdint>

namespace solar {

/**
 * Tunable behaviour of a solar-following mode. Both modes share one rule: they charge only while the available
 * current (surplus plus the grid allowance) covers minAmps. When it falls short they run at the minimum for
 * stopSustainMillis before stopping, so a short dip may import a little over the allowance. They differ in the
 * minimum current and the level needed to start. All times are in milliseconds.
 */
struct ChargingPolicy {
    int minAmps;
    int maxAmps;
    int startAmps;
    uint64_t startSustainMillis;
    uint64_t minOffMillis;
    double slowAverageSeconds;
    double fastAverageSeconds;
    int deadbandAmps;            // how far above the current limit a new one must be before stepping up
    int decreaseDeadbandAmps;    // and how far below before stepping down (smaller, to track the allowance)
    uint64_t minIncreaseIntervalMillis;
    uint64_t minDecreaseIntervalMillis;
    uint64_t stopSustainMillis = 0;   // how long the available current must stay under minAmps before a charge stops
    bool topUpToWholeAmp = false;     // Max solar: the input is the solar output alone and the grid allowance only tops it up

    /** Solar plus the allowance: minimum is the charger's 6 A. */
    static ChargingPolicy eco();

    /**
     * Max solar: the car takes what the panels make, whatever the house is using. The grid
     * allowance only tops it up to the next whole amp, or to the charger's minimum.
     */
    static ChargingPolicy maxSolar();
};

}  // namespace solar
