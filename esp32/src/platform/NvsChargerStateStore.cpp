#include "platform/NvsChargerStateStore.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include "common/Log.h"
#include "platform/Profile.h"

namespace device {

namespace {
constexpr const char* kKey = "chargers";
}  // namespace

std::map<std::string, solar::ChargerSnapshot> NvsChargerStateStore::load() {
    std::map<std::string, solar::ChargerSnapshot> result;
    Preferences prefs;
    // Read-write on purpose, so a namespace that does not exist yet is created instead of logging an error.
    if (!prefs.begin(Profile::nvsNamespace().c_str(), /*readOnly=*/false)) return result;
    String text = prefs.getString(kKey, "");
    prefs.end();
    if (text.isEmpty()) return result;

    JsonDocument doc;
    if (deserializeJson(doc, text)) {
        LOG_WARN("Saved charger state is unreadable; ignoring it.");
        return result;
    }
    for (JsonPair entry : doc.as<JsonObject>()) {
        JsonObject saved = entry.value().as<JsonObject>();
        solar::ChargerSnapshot snapshot;
        snapshot.status = static_cast<solar::ChargerStatus>(saved["s"] | static_cast<int>(solar::ChargerStatus::Unknown));
        if (!saved["t"].isNull()) snapshot.transactionId = saved["t"].as<int>();
        if (!saved["a"].isNull()) snapshot.appliedAmps = saved["a"].as<int>();
        snapshot.savedAtSeconds = saved["at"] | static_cast<int64_t>(0);
        result[entry.key().c_str()] = snapshot;
    }
    return result;
}

void NvsChargerStateStore::save(const std::map<std::string, solar::ChargerSnapshot>& snapshots) {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    for (const auto& entry : snapshots) {
        JsonObject saved = root[entry.first].to<JsonObject>();
        saved["s"] = static_cast<int>(entry.second.status);
        if (entry.second.transactionId) saved["t"] = *entry.second.transactionId;
        if (entry.second.appliedAmps) saved["a"] = *entry.second.appliedAmps;
        saved["at"] = entry.second.savedAtSeconds;
    }
    std::string text;
    serializeJson(doc, text);

    Preferences prefs;
    if (!prefs.begin(Profile::nvsNamespace().c_str(), /*readOnly=*/false)) {
        LOG_ERROR("Charger state: could not open flash storage; it will be lost on restart.");
        return;
    }
    prefs.putString(kKey, text.c_str());
    prefs.end();
}

}  // namespace device
