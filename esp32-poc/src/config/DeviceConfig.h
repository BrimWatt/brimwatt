#pragma once

#include <cstdint>

namespace device {

/**
 * The name this board announces on the network. It is what the router's device list shows (the Pi appears as
 * "wattsyncpi") and it also becomes the mDNS name, so the board is reachable as wattsync.local.
 * Letters, digits and hyphens only, 24 characters at most.
 */
constexpr uint32_t kTeslaJoinMillis = 25UL * 1000;             // Tesla mode: how long start-up waits for the home Wi-Fi
constexpr uint32_t kTeslaRejoinMillis = 10UL * 60 * 1000;      // Tesla mode: how often it looks for the home Wi-Fi again when idle
constexpr const char* kHostname = "wattsync";

constexpr uint32_t kPollIntervalMillis = 60000;        // same as the Java app
constexpr uint32_t kWifiConnectTimeoutMillis = 30000;
constexpr uint32_t kTimeSyncTimeoutMillis = 20000;

// The setup network (src/net/SetupPortal): a temporary Wi-Fi network of the board's own for choosing the home Wi-Fi.
constexpr uint32_t kSetupAfterFailMillis = 180000;       // offered after this long without being able to join the Wi-Fi
constexpr uint32_t kSetupWindowMillis = 15UL * 60 * 1000;   // closes after this long without anyone using the page
constexpr uint32_t kSetupRetryMillis = 60000;            // while it is open, try the Wi-Fi again this often (never while a phone is on it)
constexpr uint32_t kBootHoldMillis = 5000;               // hold the BOOT button this long to open it by hand
constexpr uint8_t kBootButtonPin = 0;                    // the BOOT button on the ESP32-S3 DevKit
constexpr const char* kTimezone = "PST8PDT,M3.2.0,M11.1.0";   // America/Los_Angeles: the clock rule until the owner saves their own site

constexpr const char* kEnvoyHost = "envoy";             // resolved as envoy.local

}  // namespace device
