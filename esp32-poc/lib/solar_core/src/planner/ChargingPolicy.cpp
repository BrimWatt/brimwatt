#include "planner/ChargingPolicy.h"

#include "common/Config.h"

namespace solar {

namespace {
uint64_t seconds(int s) { return static_cast<uint64_t>(s) * 1000; }
}  // namespace

ChargingPolicy ChargingPolicy::eco() {
    return {config::kMinAmps,
            config::kMaxAmps,
            config::kEcoStartAmps,
            seconds(config::kEcoStartSustainSeconds),
            seconds(config::kMinOffSeconds),
            config::kSlowAverageSeconds,
            config::kFastAverageSeconds,
            config::kDeadbandAmps,
            config::kDecreaseDeadbandAmps,
            seconds(config::kMinIncreaseIntervalSeconds),
            seconds(config::kMinDecreaseIntervalSeconds),
            seconds(config::kStopSustainSeconds)};
}

ChargingPolicy ChargingPolicy::maxSolar() {
    ChargingPolicy policy = eco();
    policy.minAmps = config::kMinAmps;
    policy.startAmps = config::kMinAmps;
    policy.topUpToWholeAmp = true;
    return policy;
}

}  // namespace solar
