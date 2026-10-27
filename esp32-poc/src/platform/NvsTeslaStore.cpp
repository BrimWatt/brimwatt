#include "platform/NvsTeslaStore.h"

#include <Preferences.h>

namespace device {

namespace {
constexpr const char* kNamespace = "tesla";
}

bool NvsTeslaStore::loadKey(solar::tesla::Bytes& privateKey) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return false;
    privateKey.assign(32, 0);
    bool ok = prefs.getBytes("key", privateKey.data(), 32) == 32;
    prefs.end();
    if (!ok) privateKey.clear();
    return ok;
}

void NvsTeslaStore::saveKey(const solar::tesla::Bytes& privateKey) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return;
    prefs.putBytes("key", privateKey.data(), privateKey.size());
    prefs.end();
}

std::string NvsTeslaStore::loadVin() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return "";
    std::string vin = prefs.getString("vin", "").c_str();
    prefs.end();
    return vin;
}

void NvsTeslaStore::saveVin(const std::string& vin) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return;
    prefs.putString("vin", vin.c_str());
    prefs.end();
}

bool NvsTeslaStore::loadWatch() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return true;
    bool on = prefs.getBool("watch", true);
    prefs.end();
    return on;
}

void NvsTeslaStore::saveWatch(bool on) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return;
    prefs.putBool("watch", on);
    prefs.end();
}

bool NvsTeslaStore::loadThrough() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return false;
    bool on = prefs.getBool("through", false);
    prefs.end();
    return on;
}

void NvsTeslaStore::saveThrough(bool car) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return;
    prefs.putBool("through", car);
    prefs.end();
}

int NvsTeslaStore::loadOnReturn() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return 0;
    int value = prefs.getUChar("onreturn", 0);
    prefs.end();
    return value;
}

void NvsTeslaStore::saveOnReturn(int value) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return;
    prefs.putUChar("onreturn", static_cast<uint8_t>(value));
    prefs.end();
}

void NvsTeslaStore::clear() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return;
    prefs.clear();
    prefs.end();
}

}  // namespace device
