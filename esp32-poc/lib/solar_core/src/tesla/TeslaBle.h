#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace solar {
namespace tesla {

/** The Bluetooth service every Tesla offers for commands (only the scan looks for the name; nothing connects). */
constexpr const char* kServiceUuid = "00000211-b2d1-43f0-9b88-960cebf8b91e";

/** 17 characters, letters and digits, none of I, O, Q (the rule every VIN follows). Case is ignored. */
bool isValidVin(const std::string& vin);

/** SHA-1 of the bytes as 20 raw bytes (needed only to build the car's Bluetooth name). */
std::vector<uint8_t> sha1(const std::string& data);

/** What a car with this VIN calls itself over Bluetooth: "S" + first 8 bytes of SHA-1(VIN) in lowercase hex + "C". Empty if the VIN is invalid. */
std::string advertisedName(const std::string& vin);

/** "5YJ***********1234": enough to recognise, not enough to share. */
std::string maskVin(const std::string& vin);

/**
 * Collects what a Bluetooth scan heard from the car and says whether the controller sits close enough.
 * Time is the scan's own clock in milliseconds. A "window" is 5 seconds; the car is heard when at least one
 * advertisement landed in it.
 */
class ScanStats {
public:
    static constexpr uint32_t kWindowMillis = 5000;

    void start(uint32_t nowMs, uint32_t durationMs);
    void heard(uint32_t nowMs, int rssi);

    int sightings() const { return sightings_; }
    int strongest() const { return sightings_ ? max_ : 0; }
    int weakest() const { return sightings_ ? min_ : 0; }
    int average() const { return sightings_ ? static_cast<int>(sum_ / sightings_) : 0; }
    /** Share of the 5-second windows in which the car was heard, 0 to 100. */
    int heardPercent() const;

    /** "Not heard", "Weak", "OK" or "Good". Based on the windows and the average signal. */
    const char* verdict() const;
    /** One plain sentence for the console. */
    std::string summary() const;

private:
    uint32_t startMs_ = 0;
    uint32_t durationMs_ = 0;
    int sightings_ = 0;
    int min_ = 0;
    int max_ = 0;
    long sum_ = 0;
    std::vector<bool> windows_;
};

}  // namespace tesla
}  // namespace solar
