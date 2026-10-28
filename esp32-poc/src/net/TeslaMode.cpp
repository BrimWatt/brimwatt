#include "net/TeslaMode.h"

#include <WiFi.h>
#include <esp_system.h>
#include <sys/time.h>

#include "ble/BleGate.h"
#include "common/Log.h"
#include "net/TimeSync.h"

namespace device {

void TeslaMode::begin() {
    on_ = store_.loadTeslaMode();
    BleGate::on() = on_;
    if (!on_) {
        LOG_INFO("Tesla mode: off. Bluetooth stays off.");
        return;
    }
    LOG_WARN("Tesla mode: ON. Bluetooth is available and the board opens its own Wi-Fi \"%s\".", solar::kTeslaHotspotName);
    startHotspot();
}

void TeslaMode::startHotspot() {
    if (portal_.isActive()) return;   // the setup network is using the access point; it is put back when that closes
    const std::string password = hotspotPassword();
    WiFi.mode(WIFI_AP_STA);   // the hotspot for the phone, next to the home Wi-Fi
    const IPAddress address(192, 168, 4, 1);
    WiFi.softAPConfig(address, address, IPAddress(255, 255, 255, 0));
    const bool ok = WiFi.softAP(solar::kTeslaHotspotName, password.c_str(), 6, 0, 4);
    // The password is never logged.
    LOG_INFO("Tesla mode: hotspot \"%s\" %s. Open http://192.168.4.1/ after joining it (the password is shown in Settings > Tesla).",
             solar::kTeslaHotspotName, ok ? "is open" : "could NOT be opened");
}

std::string TeslaMode::hotspotPassword() {
    std::string password = store_.loadHotspotPassword();
    if (password.size() < 8) {
        password = solar::generateHotspotPassword([] { return esp_random(); });
        store_.saveHotspotPassword(password);
    }
    return password;
}

solar::HotspotInfo TeslaMode::hotspot() const {
    solar::HotspotInfo info;
    info.up = on_ && !portal_.isActive() && (WiFi.getMode() & WIFI_AP) != 0;
    info.clients = info.up ? WiFi.softAPgetStationNum() : 0;
    return info;
}

void TeslaMode::setEnabled(bool on) {
    if (on == on_ || restartAt_ != 0) return;
    store_.saveTeslaMode(on);
    if (on) hotspotPassword();   // made now, so it can be read on the dashboard before the first restart is over
    LOG_WARN("Tesla mode: switched %s; restarting.", on ? "ON" : "OFF");
    restartAt_ = millis() + 1500;   // the reply reaches the phone first
}

bool TeslaMode::setClock(int64_t epochSeconds) {
    if (TimeSync::isValid()) return false;
    timeval tv;
    tv.tv_sec = static_cast<time_t>(epochSeconds);
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    LOG_INFO("Time: set from the phone (no network time yet).");
    return true;
}

void TeslaMode::loop() {
    if (restartAt_ != 0 && static_cast<int32_t>(millis() - restartAt_) >= 0) {
        if (beforeRestart_) beforeRestart_();
        delay(100);
        ESP.restart();
    }
    if (!on_ || millis() - lastCheck_ < 2000) return;
    lastCheck_ = millis();
    if (!portal_.isActive() && (WiFi.getMode() & WIFI_AP) == 0) startHotspot();   // the setup network closed
}

}  // namespace device
