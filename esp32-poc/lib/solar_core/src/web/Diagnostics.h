#pragma once

#include <cstdint>
#include <string>

namespace solar {

/** How the device is doing, for the dashboard's diagnostics section. Fields a platform cannot supply stay at their defaults and are left out of the reply. */
struct Diagnostics {
    std::string platform;                 // e.g. "ESP32-S3"
    uint32_t uptimeSeconds = 0;
    std::string lastRestart;              // why it last started, in words ("power on (normal start)")
    bool lastRestartWasCrash = false;
    int restarts = -1;                    // restarts counted, -1 if not counted
    int crashes = -1;                     // of those, how many were crashes, watchdogs or brownouts
    std::string lastCrashReason;          // empty if none recorded
    int64_t lastCrashAt = 0;              // epoch seconds
    int freeHeapKb = -1;
    int minFreeHeapKb = -1;               // the lowest it has been since the start
    int largestBlockKb = -1;
    bool hasWifi = false;
    int wifiRssi = 0;                     // dBm
    std::string firmwareBuilt;            // when this firmware was compiled
    bool clockSet = true;                 // false while the board is still waiting for network time
};

/** Supplied by the platform (the firmware, or the Pi app). */
class DiagnosticsSource {
public:
    virtual ~DiagnosticsSource() = default;
    virtual Diagnostics read() const = 0;
};

}  // namespace solar
