#include "net/WifiConnection.h"

#include <Arduino.h>
#include <WiFi.h>

#include "common/Log.h"
#include "secrets.h"

namespace device {

bool WifiConnection::connect(uint32_t timeoutMillis) {
    if (isConnected()) return true;
    LOG_INFO("Wi-Fi: connecting to \"%s\" as \"%s\" ...", WIFI_SSID, hostname_);

    // The name is what the router lists for this device (DHCP hostname); it must be set before joining.
    WiFi.setHostname(hostname_);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t start = millis();
    while (!isConnected() && millis() - start < timeoutMillis) delay(250);
    if (!isConnected()) {
        LOG_ERROR("Wi-Fi: FAILED to connect (wrong password, or a 5 GHz-only network?)");
        return false;
    }
    LOG_INFO("Wi-Fi: connected in %lu ms, IP %s, signal %d dBm, device name \"%s\"", (unsigned long)(millis() - start),
             WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.getHostname());
    return true;
}

bool WifiConnection::isConnected() const { return WiFi.status() == WL_CONNECTED; }

int WifiConnection::rssi() const { return isConnected() ? WiFi.RSSI() : 0; }

}  // namespace device
