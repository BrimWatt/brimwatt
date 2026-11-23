#pragma once

#include <optional>

#include "common/Config.h"
#include "state/ChargingMode.h"

namespace solar {

/** The settings that survive a restart. */
struct Settings {
    ChargingMode mode = ChargingMode::Eco;
    int fastModeAmps = config::kDefaultFastAmps;
    int gridAllowanceWatts = config::kGridAllowanceWatts;
    bool daylightWindowEnabled = true;
    int daylightStartMinutes = config::kDaylightStartAfterSunriseSeconds / 60;   // after sunrise
    int daylightEndMinutes = config::kDaylightEndBeforeSunsetSeconds / 60;       // before sunset
    bool paused = false;   // the kill switch
    int rateCents = config::kDefaultRateCents;
    int maxAmps = config::kMaxFastAmps;
    int peakRateCents = 0;   // 0 = no peak pricing
    int peakStartHour = 16;
    int peakEndHour = 21;
    bool peakWeekends = true;   // false: the expensive hours only apply Monday to Friday
    std::string notifyUrl;   // empty = no notifications
    int goalKwh = 0;         // 0 = no departure goal
    int goalMinutes = 420;   // by this time of day
};

/** Where settings are kept. The firmware uses the flash's non-volatile storage; tests use memory. */
class SettingsStore {
public:
    virtual ~SettingsStore() = default;

    /** Empty if nothing was saved yet. */
    virtual std::optional<Settings> load() = 0;
    virtual void save(const Settings& settings) = 0;
};

}  // namespace solar
