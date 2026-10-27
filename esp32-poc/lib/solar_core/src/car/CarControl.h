#pragma once

#include <string>
#include <vector>

#include "tesla/TeslaMessages.h"

namespace solar {

/** What the dashboard may show about the paired car. Never holds the VIN in full or any key. */
struct CarSnapshot {
    bool supported = true;            // false on the Pi (no Bluetooth link yet)
    bool paired = false;
    std::string vin;                  // masked, e.g. 5YJ**********4667
    std::string key;                  // short fingerprint of our public key
    bool watch = true;                // read the car now and then (read only)
    bool busy = false;
    std::string job;                  // "pair", "status" or "charge" while busy

    // pairing progress, meaningful while job == "pair"
    std::string pairState;            // "finding", "waiting"
    std::string pairText;
    int pairSecondsLeft = 0;

    // the job that finished last (a watch read or a test command)
    bool hasResult = false;
    std::string resultJob;
    bool resultOk = false;
    int resultAgeSeconds = 0;
    std::vector<std::string> resultLines;

    // the latest successful read
    bool hasReading = false;
    int readingAgeSeconds = 0;
    int sleep = 0;                    // 0 unknown, 1 awake, 2 asleep
    int lock = -1;
    tesla::ChargeState charge;

    int restoreAmps = 0;              // the car's setting before the last test change; 0 = nothing to restore
};

struct CarResult {
    bool ok = true;
    bool busy = false;                // refused only because another car job is running
    std::string message;              // plain words when not ok
};

/**
 * What the dashboard API needs from the car side. The firmware implements it over Bluetooth; the Pi app answers "not supported".
 * Starting a job returns at once; progress and results show up in snapshot().
 */
class CarControl {
public:
    virtual ~CarControl() = default;
    virtual CarSnapshot snapshot() const = 0;
    /** `vin` is already cleaned and checked (17 letters and digits). */
    virtual CarResult startPair(const std::string& vin) = 0;
    /** Reads the car's charge state now (read only). */
    virtual CarResult startRefresh() = 0;
    /** Test commands: they CHANGE the car. `amps` only for SetAmps. */
    virtual CarResult startStart() = 0;
    virtual CarResult startStop() = 0;
    virtual CarResult startSetAmps(int amps) = 0;
    virtual void cancel() = 0;
    virtual void setWatch(bool on) = 0;
    /** Deletes our key and the VIN from this controller (the car keeps listing the key until its owner removes it). */
    virtual void forget() = 0;
};

}  // namespace solar
