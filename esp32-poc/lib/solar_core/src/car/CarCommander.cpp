#include "car/CarCommander.h"

#include "common/Config.h"

namespace solar {

int CarCommander::backoffSeconds(int failures) {
    if (failures <= 1) return 30;
    if (failures == 2) return 60;
    if (failures == 3) return 120;
    return 300;
}

void CarCommander::setActive(bool active) {
    active_ = active;
    desired_.reset();
    fixed_.reset();
    pending_ = Job::None;
    retryAt_ = 0;
    failures_ = 0;
    unreachableSince_.reset();
    outageReported_ = false;
    stopOnReturn_ = false;
    asleep_ = false;
    problem_.clear();
    everRead_ = false;
    seenSerialSet_ = false;
    car_.setControlled(active);
}

bool CarCommander::hasConnectedChargers() const { return active_ && car_.snapshot().paired; }

void CarCommander::sendChargingLimit(int amps) {
    if (!active_) return;
    desired_ = amps < 0 ? 0 : amps;
    desiredContext_ = context_;
}

void CarCommander::holdFixedLimit(std::optional<int> amps, const std::string& context) {
    if (!active_) return;
    fixed_ = amps;
    fixedContext_ = context;
}

std::optional<int> CarCommander::wanted() const {
    if (stopOnReturn_) return 0;
    if (fixed_) return fixed_;
    return desired_;
}

void CarCommander::refreshMeasurements() {
    if (!active_) return;
    const CarSnapshot s = car_.snapshot();
    if (!s.paired || s.busy) return;
    const uint64_t now = platform_.nowMillis();
    if (now < retryAt_) return;
    if (everRead_ && now - lastReadAt_ < kReadEverySeconds * 1000ULL && fresh(s)) return;
    begin(car_.startQuietRefresh(), Job::Read, 0, now);
}

void CarCommander::begin(const CarResult& result, Job job, int amps, uint64_t now) {
    if (!result.ok) return;   // busy or refused: try again on a later pass
    pending_ = job;
    pendingAmps_ = amps;
    if (job == Job::Read) {
        lastReadAt_ = now;
        everRead_ = true;
    }
}

void CarCommander::loop() {
    if (!active_) return;
    const uint64_t now = platform_.nowMillis();
    const CarSnapshot s = car_.snapshot();
    if (!s.paired) return;

    learn(s, now);
    if (unreachableSince_ && !outageReported_ && now - *unreachableSince_ >= kOutageReportSeconds * 1000ULL && onReturn_ != CarOnReturn::Keep) {
        outageReported_ = true;
        event("Car not reachable for 10 minutes: it keeps its last setting");
    }
    if (s.busy || now < retryAt_) return;
    decide(s, now);
}

void CarCommander::learn(const CarSnapshot& s, uint64_t now) {
    if (!seenSerialSet_) {
        seenSerialSet_ = true;
        seenSerial_ = s.resultSerial;   // results from before we took command are not ours
        return;
    }
    if (s.resultSerial == seenSerial_) return;
    seenSerial_ = s.resultSerial;
    if (pending_ == Job::None) return;   // a job somebody else started (the dashboard's test buttons)
    const Job job = pending_;
    pending_ = Job::None;

    if (s.resultOk) {
        failures_ = 0;
        retryAt_ = 0;
        asleep_ = false;
        problem_.clear();
        if (job == Job::SetAmps) recordCommand("changed limit to " + std::to_string(pendingAmps_) + " A", pendingContext_);
        else if (job == Job::Start) recordCommand("started charging", pendingContext_);
        else if (job == Job::Stop) {
            recordCommand("stopped charging", pendingContext_);
            stopOnReturn_ = false;
        }
        if (unreachableSince_) recovered(now);
        return;
    }

    std::string line = s.resultLines.empty() ? std::string() : s.resultLines.back();
    problem_ = line;
    if (line.find("asleep") != std::string::npos) {
        // The car answered, it is just asleep: not an outage. Look again in a couple of minutes.
        asleep_ = true;
        retryAt_ = now + 120 * 1000ULL;
        return;
    }
    failures_++;
    retryAt_ = now + backoffSeconds(failures_) * 1000ULL;
    if (!unreachableSince_) unreachableSince_ = now;
}

void CarCommander::recovered(uint64_t now) {
    const bool longOutage = outageReported_ || (unreachableSince_ && now - *unreachableSince_ >= kOutageReportSeconds * 1000ULL);
    unreachableSince_.reset();
    if (!longOutage) {
        outageReported_ = false;
        return;
    }
    outageReported_ = false;
    if (onReturn_ == CarOnReturn::Keep) return;
    event("Car reachable again");
    if (onReturn_ == CarOnReturn::Stop) {
        stopOnReturn_ = true;
        event("Stopping the charge, as you chose for when the car comes back");
    }
}

void CarCommander::decide(const CarSnapshot& s, uint64_t now) {
    std::optional<int> want = wanted();
    if (!want) return;

    if (!fresh(s)) {
        if (!everRead_ || now - lastReadAt_ >= kReadEverySeconds * 1000ULL || !s.hasReading) {
            begin(car_.startQuietRefresh(), Job::Read, 0, now);
        }
        return;
    }
    if (s.sleep == 2 && !s.charge.valid) {   // asleep: not woken here
        asleep_ = true;
        return;
    }
    asleep_ = false;
    if (!s.charge.valid) return;

    const int code = s.charge.chargingState;
    const std::string& context = fixed_ || stopOnReturn_ ? fixedContext_ : desiredContext_;
    pendingContext_ = stopOnReturn_ ? "car was out of reach" : context;

    if (*want <= 0) {
        if (charging(code)) begin(car_.startStop(), Job::Stop, 0, now);
        else if (stopOnReturn_) stopOnReturn_ = false;   // already not charging: nothing to stop
        return;
    }
    const bool hasRequest = s.charge.hasCurrentRequest;
    const int request = hasRequest ? s.charge.currentRequest : 0;
    if (charging(code)) {
        if (!hasRequest || request != *want) begin(car_.startSetAmps(*want), Job::SetAmps, *want, now);
    } else if (code == 7) {   // stopped: set the current first, then start
        if (!hasRequest || request != *want) begin(car_.startSetAmps(*want), Job::SetAmps, *want, now);
        else begin(car_.startStart(), Job::Start, 0, now);
    }
    // disconnected, complete, no power or unknown: nothing to do
}

void CarCommander::recordCommand(const std::string& description, const std::string& context) {
    lastCommand_ = "Car: " + description;
    lastCommandAt_ = platform_.epochSeconds();
    event(context.empty() ? lastCommand_ : lastCommand_ + ": " + context);
}

double CarCommander::currentDrawAmps() {
    const CarSnapshot s = car_.snapshot();
    if (!fresh(s) || !s.charge.valid || !charging(s.charge.chargingState) || !s.charge.hasActualCurrent) return 0;
    return s.charge.actualCurrent;
}

bool CarCommander::drawIsMeasured() { return fresh(car_.snapshot()); }

std::optional<int> CarCommander::currentLimitAmps() {
    const CarSnapshot s = car_.snapshot();
    if (!fresh(s) || !s.charge.valid || !charging(s.charge.chargingState)) return std::nullopt;
    return s.charge.hasCurrentRequest ? s.charge.currentRequest : config::kMaxAmps;
}

std::optional<ChargerStatus> CarCommander::primaryStatus() {
    const CarSnapshot s = car_.snapshot();
    if (!s.hasReading || s.readingAgeSeconds > kPlugStateMaxAgeSeconds) return std::nullopt;
    if (s.sleep == 2 && !s.charge.valid) return lastStatus_;   // asleep: what we knew before
    if (!s.charge.valid) return std::nullopt;
    switch (s.charge.chargingState) {
        case 2: lastStatus_ = ChargerStatus::Available; break;
        case 3: lastStatus_ = ChargerStatus::SuspendedEVSE; break;
        case 4: case 5: case 8: lastStatus_ = ChargerStatus::Charging; break;
        case 6: lastStatus_ = ChargerStatus::Finishing; break;
        case 7: lastStatus_ = ChargerStatus::SuspendedEV; break;
        default: lastStatus_ = ChargerStatus::Unknown; break;
    }
    return lastStatus_;
}

CarCommander::Status CarCommander::status() const {
    Status st;
    st.active = active_;
    st.problem = problem_;
    if (unreachableSince_) st.unreachableSeconds = static_cast<int>((platform_.nowMillis() - *unreachableSince_) / 1000);
    if (!active_) st.state = "The charger is in command.";
    else if (asleep_) st.state = "The car is asleep. It is not woken from here: wake it with the Tesla app and WattSync carries on.";
    else if (unreachableSince_) st.state = "The car cannot be reached. It keeps its last setting; trying again.";
    else if (stopOnReturn_) st.state = "Stopping the charge, as you chose for when the car comes back.";
    else st.state = "WattSync is controlling the car's charging.";
    return st;
}

}  // namespace solar
