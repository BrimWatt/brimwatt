#include "net/WifiConnection.h"

#include <Arduino.h>
#include <WiFi.h>

#include "common/Log.h"

namespace device {

bool WifiConnection::connect(const std::string& ssid, const std::string& password, uint32_t timeoutMillis, bool keepAccessPoint) {
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

bool WifiConnection::isConnected() const { return WiFi.status() == WL_CONNECTED; }

int WifiConnection::rssi() const { return isConnected() ? WiFi.RSSI() : 0; }

}  // namespace device
