#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "car/CarControl.h"
#include "common/Platform.h"
#include "fleet/FleetApi.h"
#include "tesla/TeslaCrypto.h"
#include "tesla/TeslaFlows.h"

namespace solar {
namespace fleet {

/** Runs one job away from the main loop (the firmware starts a short task; the tests run it at once). */
class FleetExecutor {
public:
    virtual ~FleetExecutor() = default;
    /** Starts `job`; false when it cannot (no memory for a task). */
    virtual bool start(std::function<void()> job) = 0;
    virtual void sleepMs(uint32_t ms) = 0;
};

/** The car and the key that signs for it. Shared with the Bluetooth side: the same VIN and the same private key. */
class FleetIdentity {
public:
    virtual ~FleetIdentity() = default;
    virtual std::string vin() = 0;                       // empty when not set
    virtual bool privateKey(tesla::Bytes& out) = 0;      // false when there is none
    virtual void saveVin(const std::string& vin) = 0;
    virtual void saveKey(const tesla::Bytes& privateKey) = 0;
};

/**
 * "Connect through: Tesla account". The same CarControl the Bluetooth side offers, over Tesla's Fleet API:
 *  - reading the car: vehicle_data (never wakes the car; an asleep car is a real answer),
 *  - waking: wake_up, then a few reads,
 *  - charge commands (start, stop, set current): the SAME signed messages as Bluetooth, handed to the signed_command endpoint.
 * One job at a time, each in its own short task. UNTRIED on a real car: the signed_command body and the replies are assumed to
 * behave like the Bluetooth link (see docs/tesla-car-control.md).
 */
class FleetCar : public CarControl {
public:
    FleetCar(FleetApi& api, FleetAuth& auth, FleetIdentity& identity, tesla::Crypto& crypto, Platform& platform, FleetExecutor& executor)
        : api_(api), auth_(auth), identity_(identity), crypto_(crypto), platform_(platform), executor_(executor) {}

    /** Call every pass of the main loop: starts the occasional read while watching. */
    void loop();

    CarSnapshot snapshot() const override;
    CarResult startPair(const std::string& vin) override;
    CarResult startRefresh() override { return startRead(false); }
    CarResult startQuietRefresh() override { return startRead(true); }
    CarResult startStart() override { return startCharge(tesla::ChargeCommandFlow::Start, 0); }
    CarResult startStop() override { return startCharge(tesla::ChargeCommandFlow::Stop, 0); }
    CarResult startSetAmps(int amps) override { return startCharge(tesla::ChargeCommandFlow::SetAmps, amps); }
    CarResult startWake() override;
    void cancel() override {}   // a job is one short network exchange: it ends by itself
    void setControlled(bool on) override { controlled_ = on; nextReadSet_ = false; }
    void setWatch(bool on) override { watch_ = on; nextReadSet_ = false; }
    void forget() override;

    /** Reads cost money on Tesla's side, so the separate watch is rarer than over Bluetooth and starts OFF. */
    static constexpr uint32_t kWatchEverySeconds = 15 * 60;
    static constexpr int kWakeLooks = 6;
    static constexpr uint32_t kWakeLookEveryMs = 5000;

private:
    struct Result {
        bool have = false;
        std::string job;
        bool ok = false;
        uint64_t atMs = 0;
        std::vector<std::string> lines;
    };
    struct Reading {
        bool have = false;
        uint64_t atMs = 0;
        int sleep = 0;
        tesla::ChargeState charge;
    };

    CarResult refuse(const std::string& message, bool busy = false) const;
    CarResult startRead(bool quiet);
    CarResult startCharge(tesla::ChargeCommandFlow::Command command, int amps);
    CarResult launch(const std::string& job, std::function<void()> work);
    void finish(const std::string& job, bool ok, const std::vector<std::string>& lines, bool quiet);
    void noteReading(const FleetRead& read);
    bool ready(std::string& why) const;

    FleetApi& api_;
    FleetAuth& auth_;
    FleetIdentity& identity_;
    tesla::Crypto& crypto_;
    Platform& platform_;
    FleetExecutor& executor_;

    mutable std::mutex m_;
    mutable tesla::Bytes fingerprintFor_;
    mutable std::string fingerprint_;
    std::atomic<bool> running_{false};
    std::string job_;
    Result result_;
    Reading reading_;
    uint32_t serial_ = 0;
    int restoreAmps_ = 0;
    int pendingAmps_ = 0;
    bool watch_ = false;
    bool controlled_ = false;
    bool nextReadSet_ = false;
    uint64_t nextReadAt_ = 0;
    std::string lastQuietError_;
};

}  // namespace fleet
}  // namespace solar
