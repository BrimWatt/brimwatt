#pragma once

#include <cstdint>

namespace solar::config {

// Charger limits (same as the Java app's AppConfig).
constexpr int kMinAmps = 6;
constexpr int kMaxAmps = 32;                 // the most ECO and MAX_SOLAR will ask for
constexpr int kMaxFastAmps = 40;             // the most Custom (FAST) mode accepts
constexpr int kDefaultFastAmps = 32;
constexpr const char* kChargingRateUnit = "A";

// ECO / MAX_SOLAR planning (same values as the Java app's AppConfig).
constexpr int kGridAllowanceWatts = 1000;        // default; the most grid power ECO and MAX_SOLAR will import
constexpr int kMaxGridAllowanceWatts = 1000;
constexpr int kEcoStartAmps = 7;
constexpr int kEcoStartSustainSeconds = 180;   // spare solar must hold this long before a charge starts
constexpr int kMinOffSeconds = 120;
constexpr int kStopSustainSeconds = 300;      // a shortfall must last this long before a charge stops (it runs at the minimum meanwhile)
constexpr double kSlowAverageSeconds = 180;      // governs increases
constexpr double kFastAverageSeconds = 60;       // governs decreases
constexpr int kDeadbandAmps = 2;
// Stepping down reacts to a smaller gap than stepping up. Above the minimum current the only thing keeping grid
// import within the allowance is this step-down, so it is kept tight; increases stay cautious.
constexpr int kDecreaseDeadbandAmps = 1;
constexpr int kMinIncreaseIntervalSeconds = 60;
constexpr int kMinDecreaseIntervalSeconds = 30;

constexpr int kPollIntervalSeconds = 60;

// Envoy outage handling.
constexpr uint64_t kEnphaseFailsafeAfterMillis = 150000;   // stop charging after this long without readings
constexpr uint64_t kEnphaseErrorLogIntervalMillis = 60000;

// Daylight window (Lathrop, CA); ECO and MAX_SOLAR only run from sunrise + margin to sunset - margin.
constexpr double kSiteLatitude = 37.8412;
constexpr double kSiteLongitude = -121.2691;
constexpr const char* kSiteTimezone = "America/Los_Angeles";   // used until the owner saves their own location
constexpr int kDaylightStartAfterSunriseSeconds = 1800;   // the array faces south-east: mornings are strong
constexpr int kDaylightEndBeforeSunsetSeconds = 3600;
constexpr int kMaxDaylightMarginMinutes = 180;   // the margins are adjustable from 0 to this many minutes

// OCPP.
constexpr int kHeartbeatIntervalSeconds = 60;
constexpr const char* kDefaultChargerId = "DEFAULT_BOX";
constexpr int kConnectorId = 1;
constexpr int kStackLevel = 1;
constexpr const char* kOcppSubprotocol = "ocpp1.6";
constexpr uint16_t kOcppPort = 8080;

// Charger handling.
constexpr uint64_t kRemoteStartRetryMillis = 30000;     // do not send RemoteStartTransaction more often
constexpr uint64_t kMeasurementMaxAgeMillis = 120000;   // trust a MeterValues reading for this long
constexpr uint64_t kResyncFirstMillis = 5000;           // ask a new charger for its state after this
constexpr uint64_t kResyncSecondMillis = 35000;         // and again, if it still has not told us
constexpr uint64_t kChargerIdleTimeoutMillis = 300000;  // drop a charger silent for this long (it beats every 60 s)

// Savings: the electricity price used to value the sun's share of the car's energy.
constexpr int kDefaultRateCents = 30;    // cents per kWh
constexpr int kMaxRateCents = 200;
constexpr int kMaxGoalKwh = 100;   // the largest departure goal

// Saved charger state.
constexpr uint64_t kStateSaveIntervalMillis = 5000;   // look for changes this often
constexpr int64_t kStateRefreshSeconds = 300;         // even if nothing changed, refresh the timestamp this often
constexpr int64_t kStateMaxAgeSeconds = 3600;         // older saved state is ignored: the car may have left

// Used only to turn a charger's power reading into amps; 240 V on a US supply.
constexpr double kHouseholdVoltage = 240.0;

}  // namespace solar::config
