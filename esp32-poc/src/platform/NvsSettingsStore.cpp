#include "platform/NvsSettingsStore.h"

#include <Preferences.h>

#include "common/Log.h"
#include "platform/Profile.h"

namespace device {

namespace {
}

std::optional<solar::Settings> NvsSettingsStore::load() {
    Preferences prefs;
    // Opened read-write on purpose: a read-only open of a namespace that does not exist yet fails and logs an
    // error on the very first boot. Opening it for writing just creates it empty.
    if (!prefs.begin(Profile::nvsNamespace().c_str(), /*readOnly=*/false)) return std::nullopt;
    std::optional<solar::Settings> result;
    if (prefs.isKey("mode")) {
        solar::Settings settings;
        settings.mode = static_cast<solar::ChargingMode>(prefs.getUChar("mode", 0));
        settings.fastModeAmps = prefs.getInt("fastAmps", settings.fastModeAmps);
        settings.gridAllowanceWatts = prefs.getInt("allowance", settings.gridAllowanceWatts);
        settings.daylightWindowEnabled = prefs.getBool("daylight", true);
        settings.daylightStartMinutes = prefs.getInt("dayStart", settings.daylightStartMinutes);
        settings.daylightEndMinutes = prefs.getInt("dayEnd", settings.daylightEndMinutes);
        settings.paused = prefs.getBool("paused", false);
        settings.rateCents = prefs.getInt("rate", settings.rateCents);
        settings.maxAmps = prefs.getInt("maxAmps", settings.maxAmps);
        settings.peakRateCents = prefs.getInt("peakCents", settings.peakRateCents);
        settings.peakStartHour = prefs.getInt("peakFrom", settings.peakStartHour);
        settings.peakEndHour = prefs.getInt("peakTo", settings.peakEndHour);
        settings.peakWeekends = prefs.getBool("peakWknd", settings.peakWeekends);   // missing on older boards: every day
        settings.notifyUrl = prefs.getString("notifyUrl", "").c_str();
        settings.goalKwh = prefs.getInt("goalKwh", settings.goalKwh);
        settings.goalMinutes = prefs.getInt("goalMin", settings.goalMinutes);
        result = settings;
    }
    prefs.end();
    return result;
}

void NvsSettingsStore::save(const solar::Settings& settings) {
    Preferences prefs;
    if (!prefs.begin(Profile::nvsNamespace().c_str(), /*readOnly=*/false)) {
        LOG_ERROR("Settings: could not open flash storage; the change will be lost on restart.");
        return;
    }
    // Preferences only writes a value that actually changed, which spares the flash.
    prefs.putUChar("mode", static_cast<uint8_t>(settings.mode));
    prefs.putInt("fastAmps", settings.fastModeAmps);
    prefs.putInt("allowance", settings.gridAllowanceWatts);
    prefs.putBool("daylight", settings.daylightWindowEnabled);
    prefs.putInt("dayStart", settings.daylightStartMinutes);
    prefs.putInt("dayEnd", settings.daylightEndMinutes);
    prefs.putBool("paused", settings.paused);
    prefs.putInt("rate", settings.rateCents);
    prefs.putInt("maxAmps", settings.maxAmps);
    prefs.putInt("peakCents", settings.peakRateCents);
    prefs.putInt("peakFrom", settings.peakStartHour);
    prefs.putInt("peakTo", settings.peakEndHour);
    prefs.putBool("peakWknd", settings.peakWeekends);
    prefs.putString("notifyUrl", settings.notifyUrl.c_str());
    prefs.putInt("goalKwh", settings.goalKwh);
    prefs.putInt("goalMin", settings.goalMinutes);
    prefs.end();
}

}  // namespace device
