#include "net/SetupPortal.h"

#include <WiFi.h>

#include "common/Log.h"
#include "config/DeviceConfig.h"

namespace device {

namespace {

constexpr uint32_t kScanGiveUpMillis = 15000;
constexpr uint16_t kMillisPerChannel = 150;   // a scan visits 13 channels; shorter stays on each one briefly, so it ends sooner

const char* reasonText(SetupReason reason) {
    switch (reason) {
        case SetupReason::NoWifi: return "no Wi-Fi is set";
        case SetupReason::JoinFailed: return "the Wi-Fi could not be joined";
        default: return "asked for by hand";
    }
}

}  // namespace

void SetupPortal::open(SetupReason reason) {
    if (active_) return;
    active_ = true;
    reason_ = reason;
    lastActivity_ = millis();
    lastStations_ = 0;
    networks_.clear();

    // Access point plus station: the access point is for the phone, the station keeps (or retries) the home Wi-Fi.
    WiFi.mode(WIFI_AP_STA);
    const IPAddress address(192, 168, 4, 1);
    WiFi.softAPConfig(address, address, IPAddress(255, 255, 255, 0));
    WiFi.softAP(solar::setup::kApName, solar::setup::kApPassword, 6, 0, 4);

    dns_.setErrorReplyCode(DNSReplyCode::NoError);
    dns_.start(53, "*", WiFi.softAPIP());   // every name leads here, which is what makes phones open the page by themselves

    LOG_WARN("Setup: %s, so the setup network is open.", reasonText(reason));
    LOG_WARN("Setup: join the Wi-Fi \"%s\" (password \"%s\") and open http://%s/setup", solar::setup::kApName,
             solar::setup::kApPassword, solar::setup::kPortalAddress);
    startScan();
}

void SetupPortal::close() {
    if (!active_) return;
    dns_.stop();
    WiFi.scanDelete();
    scanning_ = false;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    active_ = false;
    LOG_INFO("Setup: the setup network is closed.");
}

bool SetupPortal::clientsConnected() const { return active_ && WiFi.softAPgetStationNum() > 0; }

void SetupPortal::startScan() {
    if (scanning_) return;
    WiFi.scanDelete();
    // Asynchronous, so the web server stays responsive. Hidden networks cannot be listed by name; the page offers
    // typing the name instead.
    const int started = WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false, /*passive=*/false, kMillisPerChannel);
    scanning_ = started == WIFI_SCAN_RUNNING;
    scanStarted_ = millis();
    if (!scanning_) LOG_WARN("Setup: could not start a Wi-Fi scan (code %d).", started);
}

void SetupPortal::pollScan() {
    if (!scanning_) return;
    const int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) {
        if (millis() - scanStarted_ > kScanGiveUpMillis) {
            scanning_ = false;
            WiFi.scanDelete();
            LOG_WARN("Setup: the Wi-Fi scan took too long and was dropped.");
        }
        return;
    }
    scanning_ = false;
    if (count >= 0) {
        std::vector<solar::setup::ScanEntry> raw;
        raw.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; i++) {
            raw.push_back({WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
        }
        networks_ = solar::setup::tidyScan(std::move(raw));
        LOG_INFO("Setup: scan found %d access points, %u networks listed.", count, (unsigned)networks_.size());
    }
    WiFi.scanDelete();
}

uint32_t SetupPortal::secondsLeft() const {
    if (reason_ == SetupReason::NoWifi) return 0;   // no limit: there is nothing else it could be doing
    const uint32_t used = millis() - lastActivity_;
    return used >= kSetupWindowMillis ? 1 : (kSetupWindowMillis - used) / 1000 + 1;
}

void SetupPortal::watchBootButton() {
    if (!buttonReady_) {
        pinMode(kBootButtonPin, INPUT_PULLUP);
        buttonReady_ = true;
    }
    if (digitalRead(kBootButtonPin) == HIGH) {
        pressedSince_ = 0;
        return;
    }
    if (pressedSince_ == 0) pressedSince_ = millis();
    if (!active_ && millis() - pressedSince_ >= kBootHoldMillis) {
        LOG_WARN("Setup: BOOT button held for %lu s.", (unsigned long)(kBootHoldMillis / 1000));
        open(SetupReason::Manual);
    }
}

void SetupPortal::loop() {
    watchBootButton();

    if (restartAt_ != 0 && static_cast<int32_t>(millis() - restartAt_) >= 0) {
        LOG_WARN("Setup: restarting to use the new settings.");
        if (beforeRestart_) beforeRestart_();
        delay(100);
        ESP.restart();
    }
    if (!active_) return;

    dns_.processNextRequest();
    pollScan();

    const int stations = WiFi.softAPgetStationNum();
    if (stations > lastStations_) lastActivity_ = millis();   // somebody just joined
    lastStations_ = stations;

    if (reason_ != SetupReason::NoWifi && restartAt_ == 0 && millis() - lastActivity_ > kSetupWindowMillis) {
        LOG_INFO("Setup: nobody used the setup network for %lu min.", (unsigned long)(kSetupWindowMillis / 60000));
        close();
    }
}

}  // namespace device
