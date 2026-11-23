#include "net/WifiConnection.h"

#include <Arduino.h>
#include <WiFi.h>

#include "common/Log.h"

namespace device {

bool WifiConnection::connect(const std::string& ssid, const std::string& password, uint32_t timeoutMillis, bool keepAccessPoint) {
    ssid_ = ssid;   // kept so keepAlive() can rejoin by itself
    password_ = password;
    if (isConnected()) return true;
    LOG_INFO("Wi-Fi: connecting to \"%s\" as \"%s\" ...", ssid.c_str(), hostname_);   // never the password

    // The name is what the router lists for this device (DHCP hostname); it must be set before joining.
    WiFi.setHostname(hostname_);
    WiFi.mode(keepAccessPoint ? WIFI_AP_STA : WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.disconnect(false);   // drop an earlier half-finished attempt; leaves the setup network alone
    WiFi.begin(ssid.c_str(), password.empty() ? nullptr : password.c_str());

    uint32_t start = millis();
    while (!isConnected() && millis() - start < timeoutMillis) delay(250);
    if (!isConnected()) {
        switch (WiFi.status()) {
            case WL_NO_SSID_AVAIL: lastFailure_ = "network not found (the board only joins 2.4 GHz networks)"; break;
            case WL_CONNECT_FAILED: lastFailure_ = "wrong password"; break;
            default: lastFailure_ = "no answer in time"; break;
        }
        LOG_ERROR("Wi-Fi: FAILED to connect to \"%s\": %s", ssid.c_str(), lastFailure_.c_str());
        return false;
    }
    lastFailure_.clear();
    LOG_INFO("Wi-Fi: connected in %lu ms, IP %s, signal %d dBm, device name \"%s\"", (unsigned long)(millis() - start),
             WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.getHostname());
    return true;
}

void WifiConnection::keepAlive() {
    if (ssid_.empty()) return;
    using Step = solar::WifiWatchdog::Step;
    const Step step = watchdog_.update(isConnected(), millis());
    if (watchdog_.takeRecovered()) {
        LOG_INFO("Wi-Fi: back after %lu s, IP %s, signal %d dBm", (unsigned long)watchdog_.outageSeconds(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }
    switch (step) {
        case Step::None: break;
        case Step::Reconnect:
            LOG_WARN("Wi-Fi: still down, asking the radio to reconnect.");
            WiFi.reconnect();
            break;
        case Step::Rejoin:
        case Step::ResetRadio:
            if (step == Step::ResetRadio) {
                LOG_WARN("Wi-Fi: still down, resetting the radio.");
                WiFi.disconnect(false);
                WiFi.mode(WIFI_OFF);
                delay(200);
                WiFi.setHostname(hostname_);
                WiFi.mode(WIFI_STA);
            } else {
                LOG_WARN("Wi-Fi: still down, joining \"%s\" again.", ssid_.c_str());   // never the password
                WiFi.disconnect(false);
            }
            WiFi.setAutoReconnect(true);
            WiFi.begin(ssid_.c_str(), password_.empty() ? nullptr : password_.c_str());
            break;
    }
}

bool WifiConnection::isConnected() const { return WiFi.status() == WL_CONNECTED; }

int WifiConnection::rssi() const { return isConnected() ? WiFi.RSSI() : 0; }

}  // namespace device
