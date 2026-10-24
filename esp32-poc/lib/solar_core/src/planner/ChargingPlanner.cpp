#include "planner/ChargingPlanner.h"

#include <algorithm>
#include <cmath>

namespace solar {

namespace {

/** Lets a smoothed value that is converging on a whole number (19.9 -> 20) round to it. */
constexpr double kRoundingToleranceAmps = 0.25;

/** Share of the gap to the new reading that a moving average with this time constant closes. */
double weight(double elapsedSeconds, double timeConstantSeconds) {
    return 1 - std::exp(-std::max(elapsedSeconds, 0.0) / timeConstantSeconds);
}


}  // namespace

void ChargingPlanner::reset(uint64_t nowMillis, std::optional<int> runningAmps) {
    const int64_t now = static_cast<int64_t>(nowMillis);
    slow_.reset();
    fast_.reset();
    applied_ = runningAmps;
    // A charge that is already running (arriving from FAST mode, or found after a restart) has been going for
    // an unknown time, so it may be adjusted or stopped without waiting.
    bool alreadyRunning = runningAmps.has_value();
    lastLimitChange_ = alreadyRunning ? now - static_cast<int64_t>(policy_.minIncreaseIntervalMillis) : now;
    stoppedAt_ = now - static_cast<int64_t>(policy_.minOffMillis);
    aboveSince_.reset();
    belowSince_.reset();
}

void ChargingPlanner::adoptRunning(uint64_t nowMillis, std::optional<int> runningAmps) {
    if (!runningAmps || applied_) return;
    applied_ = clamp(*runningAmps);
    // We do not know how long it has been running, so allow adjusting and stopping right away.
    lastLimitChange_ = static_cast<int64_t>(nowMillis) - static_cast<int64_t>(policy_.minIncreaseIntervalMillis);
    aboveSince_.reset();
    belowSince_.reset();
}

ChargingPlanner::Decision ChargingPlanner::next(uint64_t nowMillis, double availableAmps, double topUpAmps) {
    const int64_t now = static_cast<int64_t>(nowMillis);
    if (!slow_) {
        slow_ = availableAmps;
        fast_ = availableAmps;
    } else {
        double elapsedSeconds = static_cast<double>(now - lastTick_) / 1000.0;
        *slow_ += weight(elapsedSeconds, policy_.slowAverageSeconds) * (availableAmps - *slow_);
        *fast_ += weight(elapsedSeconds, policy_.fastAverageSeconds) * (availableAmps - *fast_);
    }
    lastTick_ = now;
    // Min of the two: falls with the fast average, rises only with the slow one.
    double smoothed = std::min(*slow_, *fast_);
    int candidate = policy_.topUpToWholeAmp ? topUpCandidate(smoothed, topUpAmps)
                                            : static_cast<int>(std::floor(smoothed + kRoundingToleranceAmps));

    return applied_ ? whileCharging(now, candidate, smoothed) : whileIdle(now, candidate, smoothed);
}

/**
 * The whole amp the car should run at when the grid may fill a gap of up to topUpAmps: the next amp up (or the charger's
 * minimum, if the sun is below it) when the gap fits, otherwise the amp below. A result under the minimum means there is
 * not enough sun even with the top-up.
 */
int ChargingPlanner::topUpCandidate(double solarAmps, double topUpAmps) const {
    int target = static_cast<int>(std::ceil(std::max(solarAmps, static_cast<double>(policy_.minAmps))));
    if (target - solarAmps <= topUpAmps + kRoundingToleranceAmps) return target;
    return static_cast<int>(std::floor(solarAmps + kRoundingToleranceAmps));
}

ChargingPlanner::Decision ChargingPlanner::whileIdle(int64_t now, int candidate, double smoothed) {
    if (candidate < policy_.startAmps) {
        aboveSince_.reset();
        return {0, smoothed, "waiting for surplus"};
    }
    if (!aboveSince_) aboveSince_ = now;

    bool sustained = now - *aboveSince_ >= static_cast<int64_t>(policy_.startSustainMillis);
    bool offLongEnough = now - stoppedAt_ >= static_cast<int64_t>(policy_.minOffMillis);
    if (!sustained || !offLongEnough) return {0, smoothed, "surplus present, confirming it lasts"};

    applied_ = clamp(candidate);
    lastLimitChange_ = now;
    aboveSince_.reset();
    belowSince_.reset();
    return {*applied_, smoothed, "start"};
}

ChargingPlanner::Decision ChargingPlanner::whileCharging(int64_t now, int candidate, double smoothed) {
    if (candidate < policy_.minAmps) {
        // Even the minimum current would import more from the grid than the allowance. Ride out a short dip at
        // the minimum; stop once the shortfall has lasted the stop delay.
        if (!belowSince_) belowSince_ = now;
        if (now - *belowSince_ >= static_cast<int64_t>(policy_.stopSustainMillis)) {
            applied_.reset();
            stoppedAt_ = now;
            aboveSince_.reset();
            belowSince_.reset();
            return {0, smoothed, "stop: surplus below the minimum, would import more than the allowance"};
        }
        return adjust(now, policy_.minAmps, smoothed, "dip: at the minimum, waiting before stopping", "dip: holding the minimum");
    }
    belowSince_.reset();
    return adjust(now, clamp(candidate), smoothed, "track surplus", "no change");
}

ChargingPlanner::Decision ChargingPlanner::adjust(int64_t now, int desired, double smoothed, const char* reason,
                                                  const char* unchangedReason) {
    int diff = desired - *applied_;
    // Stepping down reacts to a smaller gap than stepping up, so import stays near the allowance.
    bool significant = std::abs(diff) >= (diff < 0 ? policy_.decreaseDeadbandAmps : policy_.deadbandAmps);
    int64_t minInterval = static_cast<int64_t>(diff < 0 ? policy_.minDecreaseIntervalMillis : policy_.minIncreaseIntervalMillis);
    bool intervalOk = now - lastLimitChange_ >= minInterval;

    if (significant && intervalOk) {
        applied_ = desired;
        lastLimitChange_ = now;
        return {*applied_, smoothed, reason};
    }
    return {*applied_, smoothed, unchangedReason};
}

int ChargingPlanner::clamp(int amps) const { return std::min(std::max(amps, policy_.minAmps), policy_.maxAmps); }

}  // namespace solar
