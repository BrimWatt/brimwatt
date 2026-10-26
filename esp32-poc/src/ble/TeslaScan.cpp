#include "ble/TeslaScan.h"

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>

#include "common/Log.h"

namespace device {

namespace {

portMUX_TYPE statsLock = portMUX_INITIALIZER_UNLOCKED;
solar::tesla::ScanStats* activeStats = nullptr;
std::string wantedName;

/** Runs on the Bluetooth task for every advertisement heard. */
class NameMatcher : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice device) override {
        if (!device.haveName() || wantedName.empty()) return;
        if (std::string(device.getName().c_str()) != wantedName) return;
        portENTER_CRITICAL(&statsLock);
        if (activeStats) activeStats->heard(millis(), device.getRSSI());
        portEXIT_CRITICAL(&statsLock);
    }
};

NameMatcher matcher;

}  // namespace

std::string TeslaScan::start(const std::string& vin, uint32_t seconds) {
    if (running_) return "A scan is already running. Type \"tesla stop\" to end it.";
    if (!solar::tesla::isValidVin(vin)) return "That is not a VIN (17 letters and digits, no I, O or Q).";
    if (seconds < kMinSeconds || seconds > kMaxSeconds) return "Scan time must be between 10 and 300 seconds.";

    name_ = solar::tesla::advertisedName(vin);
    masked_ = solar::tesla::maskVin(vin);
    wantedName = name_;

    if (!radioOn_) {
        BLEDevice::init("");
        radioOn_ = true;
    }
    startedAt_ = millis();
    endsAt_ = startedAt_ + seconds * 1000UL;
    lastProgress_ = startedAt_;
    stats_.start(startedAt_, seconds * 1000UL);
    activeStats = &stats_;

    BLEScan* scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&matcher, true);   // true: report repeats, we want every sighting
    scan->setActiveScan(true);                            // also ask for the scan response, where a name may be
    scan->setInterval(100);
    scan->setWindow(99);
    scan->start(seconds, nullptr, false);                 // not blocking: the web server and the console keep running
    running_ = true;
    LOG_INFO("Tesla scan: listening %lu s for car %s (name %s). Nothing is sent to the car.", (unsigned long)seconds, masked_.c_str(), name_.c_str());
    return "";
}

void TeslaScan::loop() {
    if (!running_) return;
    uint32_t now = millis();
    if (now - lastProgress_ >= 10000 && now < endsAt_) {
        lastProgress_ = now;
        portENTER_CRITICAL(&statsLock);
        int n = stats_.sightings();
        portEXIT_CRITICAL(&statsLock);
        LOG_INFO("Tesla scan: %lu s left, heard the car %d times so far.", (unsigned long)((endsAt_ - now) / 1000), n);
    }
    if (now >= endsAt_ + 500) finish();
}

void TeslaScan::cancel() {
    if (running_) finish();
}

void TeslaScan::finish() {
    BLEDevice::getScan()->stop();
    portENTER_CRITICAL(&statsLock);
    activeStats = nullptr;
    portEXIT_CRITICAL(&statsLock);
    wantedName.clear();
    running_ = false;
    LOG_INFO("Tesla scan result for %s: %s", masked_.c_str(), stats_.summary().c_str());
    LOG_INFO("Tesla scan: this only shows the car is in range. It cannot show whether the car accepts charging commands over Bluetooth.");
    BLEDevice::deinit(true);   // give the memory back
    radioOn_ = false;
}

}  // namespace device
