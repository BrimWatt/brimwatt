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
 * stop), one at a time, deciding from what the car last reported. It is a ChargeDevice, so the planner cannot tell it from the wallbox.
 *
 * Rules:
 *  - It never wakes a sleeping car (that command is not built): it says "asleep" and waits.
 *  - It never sends anything while it is not in command (setActive), and never to an unpaired car.
 *  - A failed job is retried after 30 s, 60 s, 2 min, then every 5 min. Meanwhile the car keeps its last setting.
 *  - After 10 minutes out of reach: "Keep" says nothing; "Tell" reports it and reports the return; "Stop" also stops the charge once the
 *    car is back, then planning resumes as normal.
 *  - Decisions use the car's reading only while it is under 150 s old; an older one is read again first.
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

    struct Status {
        bool active = false;
        std::string state;            // plain words: what it is doing or waiting for
        int unreachableSeconds = 0;   // 0 when the car answered the last job
        std::string problem;          // the last failed job's own words
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
    bool drawIsMeasured() override;
    std::optional<int> currentLimitAmps() override;
    bool anyRestored() override { return false; }
    std::optional<ChargerStatus> primaryStatus() override;
    const std::string& lastCommand() const override { return lastCommand_; }
    int64_t lastCommandAtSeconds() const override { return lastCommandAt_; }

    static constexpr int kFreshSeconds = 150;
    static constexpr int kReadEverySeconds = 45;
    static constexpr int kPlugStateMaxAgeSeconds = 600;
    static constexpr int kOutageReportSeconds = 600;

private:
    enum class Job { None, Read, SetAmps, Start, Stop };
    static int backoffSeconds(int failures);
    bool fresh(const CarSnapshot& s) const { return s.hasReading && s.readingAgeSeconds < kFreshSeconds; }
    void learn(const CarSnapshot& s, uint64_t now);
    void decide(const CarSnapshot& s, uint64_t now);
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

    std::optional<int> desired_;
    std::string desiredContext_;
    std::optional<int> fixed_;
    std::string fixedContext_;
    std::string context_;

    Job pending_ = Job::None;
    int pendingAmps_ = 0;
    std::string pendingContext_;
    uint32_t seenSerial_ = 0;
    bool seenSerialSet_ = false;
    uint64_t retryAt_ = 0;
    uint64_t lastReadAt_ = 0;
    bool everRead_ = false;
    int failures_ = 0;
    std::optional<uint64_t> unreachableSince_;
    bool outageReported_ = false;
    bool stopOnReturn_ = false;
    bool asleep_ = false;
    std::string problem_;
    std::optional<ChargerStatus> lastStatus_;

    std::string lastCommand_ = "None yet";
    int64_t lastCommandAt_ = 0;
};

}  // namespace solar
