#include "ble/TeslaScan.h"

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>

#include "common/Log.h"

namespace device {

namespace {

portMUX_TYPE statsLock = portMUX_INITIALIZER_UNLOCKED;
solar::tesla::ScanStats* activeStats = nullptr;
solar::tesla::Discovery* activeDiscovery = nullptr;
std::string wantedName;

/** Runs on the Bluetooth task for every advertisement heard. */
class NameMatcher : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice device) override {
        std::string name = device.haveName() ? std::string(device.getName().c_str()) : std::string();
        if (activeDiscovery) {
            bool tesla = solar::tesla::isTeslaName(name) || device.isAdvertisingService(BLEUUID(solar::tesla::kServiceUuid));
            if (!tesla) return;
            portENTER_CRITICAL(&statsLock);
            if (activeDiscovery) activeDiscovery->heard(millis(), solar::tesla::isTeslaName(name) ? name.c_str() : nullptr, device.getRSSI());
            portEXIT_CRITICAL(&statsLock);
            return;
        }
        if (wantedName.empty() || name != wantedName) return;
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
    discover_ = false;
    name_ = solar::tesla::advertisedName(vin);
    masked_ = solar::tesla::maskVin(vin);
    wantedName = name_;
    std::string err = begin(seconds);
    if (err.empty()) LOG_INFO("Tesla scan: listening %lu s for car %s (name %s). Nothing is sent to the car.", (unsigned long)seconds, masked_.c_str(), name_.c_str());
    return err;
}

std::string TeslaScan::startDiscover(uint32_t seconds) {
    if (running_) return "A scan is already running. Type \"tesla stop\" to end it.";
    if (seconds < kMinSeconds || seconds > kMaxSeconds) return "Scan time must be between 10 and 300 seconds.";
    discover_ = true;
    masked_ = "any Tesla";
    wantedName.clear();
    std::string err = begin(seconds);
    if (err.empty()) LOG_INFO("Tesla scan: listening %lu s for any Tesla in range. Nothing is sent to anyone.", (unsigned long)seconds);
    return err;
}

std::string TeslaScan::begin(uint32_t seconds) {
    if (!radioOn_) {
        BLEDevice::init("");
        radioOn_ = true;
    }
    startedAt_ = millis();
    endsAt_ = startedAt_ + seconds * 1000UL;
    lastProgress_ = startedAt_;
    stats_.start(startedAt_, seconds * 1000UL);
    discovery_.start(startedAt_, seconds * 1000UL);
    activeStats = discover_ ? nullptr : &stats_;
    activeDiscovery = discover_ ? &discovery_ : nullptr;

    BLEScan* scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&matcher, true);   // true: report repeats, we want every sighting
    scan->setActiveScan(true);                            // also ask for the scan response, where a name may be
    scan->setInterval(100);
    scan->setWindow(99);
    scan->start(seconds, nullptr, false);                 // not blocking: the web server and the console keep running
    running_ = true;
    return "";
}

void TeslaScan::loop() {
    if (!running_) return;
    uint32_t now = millis();
    if (now - lastProgress_ >= 10000 && now < endsAt_) {
        lastProgress_ = now;
        portENTER_CRITICAL(&statsLock);
        int n = discover_ ? discovery_.count() : stats_.sightings();
        portEXIT_CRITICAL(&statsLock);
        LOG_INFO("Tesla scan: %lu s left, %d %s so far.", (unsigned long)((endsAt_ - now) / 1000), n, discover_ ? "car(s) heard" : "sighting(s)");
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
    activeDiscovery = nullptr;
    portEXIT_CRITICAL(&statsLock);
    wantedName.clear();
    running_ = false;
    if (discover_) {
        auto lines = discovery_.lines();
        if (lines.empty()) {
            LOG_INFO("Tesla scan: no Tesla heard. Move the board closer to the car and try again.");
        } else {
            LOG_INFO("Tesla scan: %d Tesla%s heard, strongest first. Yours is most likely the first one; scan again with the car moved to check.", (int)lines.size(),
                     lines.size() == 1 ? "" : "s");
            for (const auto& l : lines) LOG_INFO("  %s", l.c_str());
            if (discovery_.overflowed()) LOG_INFO("  (more cars were heard than are listed)");
        }
    } else {
        LOG_INFO("Tesla scan result for %s: %s", masked_.c_str(), stats_.summary().c_str());
    }
    LOG_INFO("Tesla scan: this only shows the car is in range. It cannot show whether the car accepts charging commands over Bluetooth.");
    BLEDevice::deinit(true);   // give the memory back
    radioOn_ = false;
}

}  // namespace device
