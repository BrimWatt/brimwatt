#pragma once

#include <cstdint>

namespace device {

/**
 * The name this board announces on the network. It is what the router's device list shows (the Pi appears as
 * "wattsyncpi") and it also becomes the mDNS name, so the board is reachable as wattsyncesp.local.
 * Letters, digits and hyphens only, 24 characters at most.
 */
constexpr const char* kHostname = "wattsyncesp";

constexpr uint32_t kPollIntervalMillis = 60000;        // same as the Java app
constexpr uint32_t kWifiConnectTimeoutMillis = 30000;
constexpr uint32_t kTimeSyncTimeoutMillis = 20000;
constexpr const char* kTimezone = "PST8PDT,M3.2.0,M11.1.0";   // America/Los_Angeles, for the log only

constexpr const char* kEnvoyHost = "envoy";             // resolved as envoy.local

}  // namespace device
