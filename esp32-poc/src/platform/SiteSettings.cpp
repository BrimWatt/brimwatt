#include "platform/SiteSettings.h"

#include <Preferences.h>

#include "common/Log.h"

namespace device {

namespace {
constexpr const char* kNamespace = "site";   // at most 15 characters
}

void SiteSettings::begin() {
    Preferences prefs;
    // Opened read-write on purpose, as in the other stores: a read-only open of an area that does not exist yet fails noisily.
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return;
    if (prefs.getBool("set", false)) {
        solar::Site saved;
        saved.latitude = prefs.getDouble("lat", saved.latitude);
        saved.longitude = prefs.getDouble("lon", saved.longitude);
        saved.timezone = prefs.getString("tz", saved.timezone.c_str()).c_str();
        saved.name = prefs.getString("name", "").c_str();
        // A damaged or unknown zone must never leave the board without a clock rule: fall back to the default.
        if (solar::timezones::posixFor(saved.timezone)) {
            site_ = saved;
            set_ = true;
        } else {
            LOG_WARN("Site: the saved time zone is not one this board knows; using the default.");
        }
    }
    prefs.end();
    LOG_INFO("Site: %s (%s)%s.", site_.name.empty() ? "default location" : site_.name.c_str(), site_.timezone.c_str(), set_ ? "" : ", not set by the owner yet");
}

void SiteSettings::save(const solar::Site& site) {
    if (restartAt_ != 0) return;
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
        LOG_ERROR("Site: could not open flash storage; the location was not saved.");
        return;
    }
    prefs.putDouble("lat", site.latitude);
    prefs.putDouble("lon", site.longitude);
    prefs.putString("tz", site.timezone.c_str());
    prefs.putString("name", site.name.c_str());
    prefs.putBool("set", true);
    prefs.end();
    LOG_WARN("Site: saved %s (%s); restarting to use it.", site.name.empty() ? "a new location" : site.name.c_str(), site.timezone.c_str());
    restartAt_ = millis() + 1500;   // the reply reaches the phone first
}

void SiteSettings::loop() {
    if (restartAt_ != 0 && static_cast<int32_t>(millis() - restartAt_) >= 0) {
        if (beforeRestart_) beforeRestart_();
        delay(100);
        ESP.restart();
    }
}

}  // namespace device
