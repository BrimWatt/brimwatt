#pragma once

#include <cstdint>
#include <algorithm>
#include <optional>
#include <string>

#include "planner/ChargingPolicy.h"

namespace solar {

/**
 * Decides the charging current from a stream of "available amps" readings, smoothing out short-lived swings so
 * the charger is not reconfigured on every poll.
 *
 * Available amps = what the charger is already drawing + the surplus still being exported (+ the grid
 * allowance, added by the caller). Using the draw as well avoids a feedback loop: the charger's own load
 * shrinks the exported surplus, so surplus alone would make the limit oscillate even under constant sun.
 */
class ChargingPlanner {
public:
    struct Decision {
        int amps;               // target current, 0 meaning "not charging"
        double smoothedAmps;
        std::string reason;
    };

    explicit ChargingPlanner(ChargingPolicy policy) : policy_(policy), ceilingAmps_(policy.maxAmps) { reset(0, std::nullopt); }

    /** Applies the installation's maximum current (the setting); never more than this or the policy's own ceiling. */
    void setMaxAmps(int maxAmps) { policy_.maxAmps = std::max(policy_.minAmps, std::min(ceilingAmps_, maxAmps)); }

    /** Forgets history, e.g. when the mode changes. runningAmps is the limit in effect if a charge is running. */
    void reset(uint64_t nowMillis, std::optional<int> runningAmps);

    /** Starts the averages from the next reading without touching the start timers (allowance changed). */
    void reseedAverages() {
        slow_.reset();
        fast_.reset();
    }

    /**
     * Brings the planner in line with a charge that is already running but that it did not start (after a
     * restart). Without this it would believe nothing is charging and stop a car that should keep going.
     */
    void adoptRunning(uint64_t nowMillis, std::optional<int> runningAmps);

    /**
     * `topUpAmps` is how much the grid may add, in amps; it is used only by a policy that tops up to a whole amp (Max
     * solar), where `availableAmps` is the solar output alone.
     */
    Decision next(uint64_t nowMillis, double availableAmps, double topUpAmps = 0);

private:
    int topUpCandidate(double solarAmps, double topUpAmps) const;
    Decision whileIdle(int64_t now, int candidate, double smoothed);
    Decision whileCharging(int64_t now, int candidate, double smoothed);
    Decision adjust(int64_t now, int desired, double smoothed, const char* reason, const char* unchangedReason);
    int clamp(int amps) const;

    ChargingPolicy policy_;
    int ceilingAmps_;   // the policy's own maximum; the installation's limit can only lower it
    std::optional<double> slow_;
    std::optional<double> fast_;
    int64_t lastTick_ = 0;
    std::optional<int> applied_;        // empty while not charging
    // Signed on purpose: "already running" is expressed as a start time in the past, which can be before boot.
    int64_t lastLimitChange_ = 0;
    int64_t stoppedAt_ = 0;
    std::optional<int64_t> aboveSince_;
    std::optional<int64_t> belowSince_;   // when the available current first fell short of the minimum, while charging
};

}  // namespace solar
