#pragma once

#include <Arduino.h>

#include <memory>
#include <string>
#include <vector>

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
    enum class Job { None, Pair, Status, Charge, Wake };

    /** How the last finished job went (any job, including the quiet watch reads). */
    struct Result {
        bool have = false;
        const char* job = "";   // "pair", "status" or "charge"
        bool ok = false;
        uint32_t atMs = 0;
        std::vector<std::string> lines;
    };
    /** The latest successful read of the car: from a status read, or the read-back after a test command. */
    struct Reading {
        bool have = false;
        uint32_t atMs = 0;
        int sleep = 0;          // 0 unknown, 1 awake, 2 asleep
        int lock = -1;
        solar::tesla::ChargeState charge;
    };

    explicit TeslaCar(NvsTeslaStore& store) : store_(store) {}

    /** Starts pairing with the car that has this VIN. Returns an error text, or empty when it started. */
    std::string startPair(const std::string& vin);
    /** Starts the read-only status for the paired car. Returns an error text, or empty when it started. */
    std::string startStatus(bool quiet = false);
    /**
     * Starts one charge command for the paired car (start, stop, or set the current). This CHANGES the car. Returns an error text, or empty
     * when it started. The amps are checked here, before anything is sent.
     */
    std::string startCharge(solar::tesla::ChargeCommandFlow::Command command, int amps);
    /** Wakes a sleeping car (the owner's call; it stays awake a while). Returns an error text, or empty when it started. */
    std::string startWake();
    void loop();
    void cancel();
    /** Prints every message to and from the car as hex (no secrets in them). Off by default; for trying a new car model. */
    void setDebug(bool on) { debug_ = on; }
    bool debug() const { return debug_; }
    bool busy() const { return job_ != Job::None; }
    const char* jobName() const { return job_ == Job::Pair ? "pair" : job_ == Job::Status ? "status" : job_ == Job::Charge ? "charge" : job_ == Job::Wake ? "wake" : ""; }
    const Result& lastResult() const { return result_; }
    /** How many jobs have finished since start-up (so a listener can tell a new result from an old one). */
    uint32_t finishedCount() const { return finished_; }
    const Reading& lastReading() const { return reading_; }
    /** While pairing: "finding" or "waiting", the car's latest words, and how long until the 2-minute limit. */
    const char* pairState() const { return stage_ == Stage::Finding ? "finding" : "waiting"; }
    std::string pairText() const;
    int pairSecondsLeft() const;

    /** A short, shareable fingerprint of our public key (first 4 bytes of its SHA-256), or empty when there is no key yet. */
    std::string keyFingerprint();
    bool hasKey();
    bool paired();
    /** Deletes our key and the VIN from this board (the car keeps listing the key until the owner removes it there). */
    void forget();
    /** Runs Tesla's published example values through this board's cryptography. */
    bool selfTest(std::string& report);

    static constexpr uint32_t kFindSeconds = 20;
    static constexpr int kScanRounds = 2;   // looks for the car before giving up

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
    bool debug_ = false;
    bool quiet_ = false;
    uint32_t finished_ = 0;
    Result result_;
    Reading reading_;
    uint32_t startedAt_ = 0;
    uint32_t scanStartedAt_ = 0;
    int scanRound_ = 1;
    uint32_t lastActivity_ = 0;
    uint32_t lastProgress_ = 0;
    std::string rx_;
    std::unique_ptr<solar::tesla::PairFlow> pair_;
    std::unique_ptr<solar::tesla::StatusFlow> status_;
    std::unique_ptr<solar::tesla::ChargeCommandFlow> charge_;
    std::unique_ptr<solar::tesla::WakeFlow> wake_;
    std::string chargeWhat_;
    int lastPairState_ = -1;
};

}  // namespace device
