#pragma once

#include <Arduino.h>

#include <memory>
#include <string>

#include "ble/MbedTeslaCrypto.h"
#include "platform/NvsTeslaStore.h"
#include "tesla/TeslaFlows.h"

class BLEClient;

namespace device {

/**
 * Talks to one Tesla over Bluetooth, one job at a time: pair (add our key; the owner taps their key card), read the status
 * (read-only), or send one charge command (start, stop, set amps). Started from the console, advanced from the main loop, never blocks for long, and gives the radio back when done.
 * The first connection step is the only one that blocks (a few seconds).
 */
class TeslaCar {
public:
    enum class Job { None, Pair, Status, Charge };

    explicit TeslaCar(NvsTeslaStore& store) : store_(store) {}

    /** Starts pairing with the car that has this VIN. Returns an error text, or empty when it started. */
    std::string startPair(const std::string& vin);
    /** Starts the read-only status for the paired car. Returns an error text, or empty when it started. */
    std::string startStatus();
    /**
     * Starts one charge command for the paired car (start, stop, or set the current). This CHANGES the car. Returns an error text, or empty
     * when it started. The amps are checked here, before anything is sent.
     */
    std::string startCharge(solar::tesla::ChargeCommandFlow::Command command, int amps);
    void loop();
    void cancel();
    bool busy() const { return job_ != Job::None; }

    /** A short, shareable fingerprint of our public key (first 4 bytes of its SHA-256), or empty when there is no key yet. */
    std::string keyFingerprint();
    bool hasKey();
    bool paired();
    /** Deletes our key and the VIN from this board (the car keeps listing the key until the owner removes it there). */
    void forget();
    /** Runs Tesla's published example values through this board's cryptography. */
    bool selfTest(std::string& report);

    static constexpr uint32_t kFindSeconds = 20;

private:
    enum class Stage { Idle, Finding, Running };

    void begin(Job job);
    void finish(const std::string& why);
    bool connect();
    void sendMessage(const solar::tesla::Bytes& routable);
    void handleFrames();
    bool ensureKey();

    NvsTeslaStore& store_;
    MbedTeslaCrypto crypto_;
    Job job_ = Job::None;
    Stage stage_ = Stage::Idle;
    std::string vin_;
    std::string name_;
    solar::tesla::Bytes priv_, pub_;
    BLEClient* client_ = nullptr;
    void* write_ = nullptr;   // BLERemoteCharacteristic*
    bool radioOn_ = false;
    bool scanFailed_ = false;
    uint32_t startedAt_ = 0;
    uint32_t lastActivity_ = 0;
    uint32_t lastProgress_ = 0;
    std::string rx_;
    std::unique_ptr<solar::tesla::PairFlow> pair_;
    std::unique_ptr<solar::tesla::StatusFlow> status_;
    std::unique_ptr<solar::tesla::ChargeCommandFlow> charge_;
    std::string chargeWhat_;
    int lastPairState_ = -1;
};

}  // namespace device
