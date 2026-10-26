#pragma once

#include <Arduino.h>

#include <string>

#include "tesla/TeslaBle.h"

namespace device {

/**
 * Phase 0 of car control: listens for one car's Bluetooth advertisements and reports how well this board hears it.
 * It never connects, pairs or writes anything to the car. The radio is only switched on for the scan and released
 * afterwards, so the Wi-Fi side is untouched the rest of the time.
 */
class TeslaScan {
public:
    /** Starts a scan for the car with this VIN (kept in memory only for the scan). Returns an error text, or empty when it started. */
    std::string start(const std::string& vin, uint32_t seconds);
    /** Starts a scan that lists every Tesla it hears (no VIN needed). Returns an error text, or empty when it started. */
    std::string startDiscover(uint32_t seconds);
    /** Call from the main loop: prints progress and the result, then frees the radio. */
    void loop();
    bool running() const { return running_; }
    void cancel();

    static constexpr uint32_t kMinSeconds = 10;
    static constexpr uint32_t kMaxSeconds = 300;

private:
    void finish();
    std::string begin(uint32_t seconds);

    bool running_ = false;
    bool radioOn_ = false;
    uint32_t startedAt_ = 0;
    uint32_t endsAt_ = 0;
    uint32_t lastProgress_ = 0;
    std::string name_;
    std::string masked_;
    bool discover_ = false;
    solar::tesla::ScanStats stats_;
    solar::tesla::Discovery discovery_;
};

}  // namespace device
