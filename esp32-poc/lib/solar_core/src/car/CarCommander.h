#pragma once

#include <optional>
#include <string>

#include "car/CarControl.h"
#include "common/Platform.h"
#include "history/HistoryService.h"
#include "wallbox/ChargeDevice.h"

namespace solar {

/** What to do when the car comes back after being out of reach for 10 minutes or more. */
enum class CarOnReturn { Keep = 0, Tell = 1, Stop = 2 };

/**
 * "Control through: Car". Takes the current the planner wants and turns it into Bluetooth jobs for the car (set the current, start,
 * stop, and when allowed wake it), one at a time. It is a ChargeDevice, so the planner cannot tell it from the wallbox.
 *
 * WattSync is the only thing meant to set the car's current, so the commander works from what it believes the car is doing: what the
 * car last reported, updated by every command it sends. It does not measure the car. The car is read only to CHECK that belief:
 * after each command (the read-back), when it takes command or after an outage, and every 10 minutes (the car may have stopped by
 * itself, been unplugged, or had its current changed by someone else; the next decision then corrects it).
 *
 * Rules:
 *  - A sleeping car is only woken if the owner allowed it, only when charging is wanted and the car was last seen plugged in, and at
 *    most once every 30 minutes.
 *  - It never sends anything while it is not in command (setActive), and never to an unpaired car.
 *  - A failed job is retried after 30 s, 60 s, 2 min, then every 5 min. Meanwhile the car keeps its last setting.
 *  - After 10 minutes out of reach: "Keep" says nothing; "Tell" reports it and the return; "Stop" also stops the charge once the
 *    car is back, then planning resumes as normal.
 */
class CarCommander : public ChargeDevice {
public:
    CarCommander(Platform& platform, CarControl& car) : platform_(platform), car_(car) {}

    /** Call every pass of the main loop. */
    void loop();

    /** The car takes command (true) or lets go (false). Starting fresh each time. */
    void setActive(bool active);
    bool active() const { return active_; }
    void setOnReturn(CarOnReturn policy) { onReturn_ = policy; }
    CarOnReturn onReturn() const { return onReturn_; }
    /** May a sleeping car be woken when charging is wanted? Off until the owner says so. */
    void setAllowWake(bool on) { allowWake_ = on; }
    bool allowWake() const { return allowWake_; }

    struct Status {
        bool active = false;
        std::string state;            // plain words: what it is doing or waiting for
        int unreachableSeconds = 0;   // 0 when the car answered the last job
        std::string problem;          // the last failed job's own words
        bool allowWake = false;
        int lastWakeAgeSeconds = -1;  // -1: never woken since start-up
    };
    Status status() const;

    // ---- ChargeDevice ----
    bool hasConnectedChargers() const override;
    void sendChargingLimit(int amps) override;
    void holdFixedLimit(std::optional<int> amps, const std::string& context = "") override;
    void setHistory(HistoryService* history) override { history_ = history; }
    void setCommandContext(const std::string& context) override { context_ = context; }
    void refreshMeasurements() override;
    double currentDrawAmps() override;
    bool drawIsMeasured() override { return false; }   // assumed from what WattSync set
    std::optional<int> currentLimitAmps() override;
    bool anyRestored() override { return false; }
    std::optional<ChargerStatus> primaryStatus() override;
    const std::string& lastCommand() const override { return lastCommand_; }
    int64_t lastCommandAtSeconds() const override { return lastCommandAt_; }

    static constexpr int kCheckEverySeconds = 600;
    static constexpr int kFreshReadSeconds = 3;       // a reading this young was taken by the job that just finished
    static constexpr int kOutageReportSeconds = 600;
    static constexpr int kWakeEverySeconds = 1800;

private:
    enum class Job { None, Read, SetAmps, Start, Stop, Wake };
    struct Belief {
        bool known = false;
        int code = 1;                 // the car's charging state: 2 disconnected, 3 no power, 4 starting, 5 charging, 6 complete, 7 stopped, 8 calibrating
        bool hasRequest = false;
        int request = 0;              // the current the car is set to
        bool asleep = false;
    };
    static int backoffSeconds(int failures);
    void learn(const CarSnapshot& s, uint64_t now);
    void adopt(const CarSnapshot& s, uint64_t now);
    void decide(uint64_t now);
    void maybeWake(uint64_t now);
    void begin(const CarResult& result, Job job, int amps, uint64_t now);
    void recordCommand(const std::string& description, const std::string& context);
    void recovered(uint64_t now);
    void event(const std::string& text) { if (history_) history_->addEvent(text); }
    static bool charging(int code) { return code == 4 || code == 5 || code == 8; }
    std::optional<int> wanted() const;

    Platform& platform_;
    CarControl& car_;
    HistoryService* history_ = nullptr;
    bool active_ = false;
    CarOnReturn onReturn_ = CarOnReturn::Keep;
    bool allowWake_ = false;

    std::optional<int> desired_;
    std::string desiredContext_;
    std::optional<int> fixed_;
    std::string fixedContext_;
    std::string context_;

    Belief belief_;
    std::optional<ChargerStatus> lastStatus_;
    uint64_t nextCheckAt_ = 0;
    Job pending_ = Job::None;
    int pendingAmps_ = 0;
    std::string pendingContext_;
    uint32_t seenSerial_ = 0;
    bool seenSerialSet_ = false;
    uint64_t retryAt_ = 0;
    int failures_ = 0;
    std::optional<uint64_t> unreachableSince_;
    bool outageReported_ = false;
    bool stopOnReturn_ = false;
    std::optional<uint64_t> lastWakeAt_;
    std::string problem_;

    std::string lastCommand_ = "None yet";
    int64_t lastCommandAt_ = 0;
};

}  // namespace solar
