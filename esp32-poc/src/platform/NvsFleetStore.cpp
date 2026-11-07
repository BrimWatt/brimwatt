#include "platform/NvsFleetStore.h"

#include <Preferences.h>

namespace device {

namespace {
constexpr const char* kNamespace = "fleet";
}

bool NvsFleetStore::load(solar::fleet::FleetCredentials& out) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return false;
    out.clientId = prefs.getString("cid", "").c_str();
    out.refreshToken = prefs.getString("rtok", "").c_str();
    out.region = prefs.getString("reg", "na").c_str();
    prefs.end();
    return out.complete();
}

void NvsFleetStore::save(const solar::fleet::FleetCredentials& in) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return;
    prefs.putString("cid", in.clientId.c_str());
    prefs.putString("rtok", in.refreshToken.c_str());
    prefs.putString("reg", in.region.c_str());
    prefs.end();
}

bool NvsFleetStore::loadKey(solar::tesla::Bytes& out) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return false;
    out.assign(32, 0);
    const bool ok = prefs.getBytes("fkey", out.data(), 32) == 32;
    prefs.end();
    if (!ok) out.clear();
    return ok;
}

void NvsFleetStore::saveKey(const solar::tesla::Bytes& key) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return;
    prefs.putBytes("fkey", key.data(), key.size());
    prefs.end();
}

void NvsFleetStore::clear() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return;
    prefs.clear();
    prefs.end();
}

}  // namespace device
