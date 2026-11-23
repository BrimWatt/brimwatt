#include "platform/NvsCredentialStore.h"

#include <Preferences.h>

#include "common/Log.h"

namespace device {

namespace {
constexpr const char* kNamespace = "net";
}

solar::setup::Credentials NvsCredentialStore::load() {
    solar::setup::Credentials credentials;
    Preferences prefs;
    // Opened read-write on purpose, as in NvsSettingsStore: a read-only open of an area that does not exist yet fails noisily.
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return credentials;
    credentials.wifiSsid = prefs.getString("wifiSsid", "").c_str();
    credentials.wifiPassword = prefs.getString("wifiPass", "").c_str();
    credentials.enphaseToken = prefs.getString("token", "").c_str();
    credentials.envoyIp = prefs.getString("envoyIp", "").c_str();
    credentials.otaPassword = prefs.getString("otaPass", "").c_str();
    prefs.end();
    return credentials;
}

void NvsCredentialStore::save(const solar::setup::Credentials& credentials) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
        LOG_ERROR("Setup: could not open flash storage; the settings will be lost on restart.");
        return;
    }
    // Preferences only writes a value that actually changed, which spares the flash.
    prefs.putString("wifiSsid", credentials.wifiSsid.c_str());
    prefs.putString("wifiPass", credentials.wifiPassword.c_str());
    prefs.putString("token", credentials.enphaseToken.c_str());
    prefs.putString("envoyIp", credentials.envoyIp.c_str());
    prefs.putString("otaPass", credentials.otaPassword.c_str());
    prefs.end();
}

solar::setup::TrialState NvsCredentialStore::loadTrial() {
    solar::setup::TrialState trial;
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return trial;
    trial.pending = prefs.getBool("trial", false);
    trial.previousSsid = prefs.getString("prevSsid", "").c_str();
    trial.previousPassword = prefs.getString("prevPass", "").c_str();
    trial.lastError = prefs.getString("setupErr", "").c_str();
    prefs.end();
    return trial;
}

void NvsCredentialStore::saveTrial(const solar::setup::TrialState& trial) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return;
    prefs.putBool("trial", trial.pending);
    prefs.putString("prevSsid", trial.previousSsid.c_str());
    prefs.putString("prevPass", trial.previousPassword.c_str());
    prefs.putString("setupErr", trial.lastError.c_str());
    prefs.end();
}

void NvsCredentialStore::clear() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return;
    prefs.clear();
    prefs.end();
}

}  // namespace device
