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
    belief_ = Belief();
    lastStatus_.reset();
    nextCheckAt_ = 0;
    pending_ = Job::None;
    retryAt_ = 0;
    failures_ = 0;
    unreachableSince_.reset();
    outageReported_ = false;
    stopOnReturn_ = false;
    problem_.clear();
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
    if (!active_ || belief_.known) return;   // nothing to measure: it only needs a first look at the car
    const CarSnapshot s = car_.snapshot();
    const uint64_t now = platform_.nowMillis();
    if (!s.paired || s.busy || now < retryAt_ || pending_ != Job::None) return;
    begin(car_.startQuietRefresh(), Job::Read, 0, now);
}

void CarCommander::begin(const CarResult& result, Job job, int amps, uint64_t now) {
    (void)now;
    if (!result.ok) return;   // busy or refused: try again on a later pass
    pending_ = job;
    pendingAmps_ = amps;
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
    if (s.busy || now < retryAt_ || pending_ != Job::None) return;
    decide(now);
}

/** Takes a reading into the belief, if it is the one the job that just finished produced. */
void CarCommander::adopt(const CarSnapshot& s, uint64_t now) {
    if (!s.hasReading || s.readingAgeSeconds > kFreshReadSeconds) return;
    if (s.sleep == 2 && !s.charge.valid) {   // asleep: no charge data, keep what we knew about the plug and the current
        belief_.known = true;
        belief_.asleep = true;
    } else if (s.charge.valid) {
        belief_.known = true;
        belief_.asleep = false;
        belief_.code = s.charge.chargingState;
        belief_.hasRequest = s.charge.hasCurrentRequest;
        belief_.request = s.charge.currentRequest;
    } else {
        return;
    }
    nextCheckAt_ = now + kCheckEverySeconds * 1000ULL;
}

void CarCommander::learn(const CarSnapshot& s, uint64_t now) {
    if (!seenSerialSet_) {
        seenSerialSet_ = true;
        seenSerial_ = s.resultSerial;   // results from before we took command are not ours, but a fresh reading is still news
        adopt(s, now);
        return;
    }
    if (s.resultSerial == seenSerial_) return;
    seenSerial_ = s.resultSerial;
    const Job job = pending_;
    pending_ = Job::None;
    if (job == Job::None) {   // a job somebody else started (the dashboard's buttons): its reading is still news
        if (s.resultOk) adopt(s, now);
        return;
    }

    if (s.resultOk) {
        failures_ = 0;
        retryAt_ = 0;
        problem_.clear();
        // What we just did, taken as true until a reading says otherwise.
        if (job == Job::SetAmps) {
            belief_.hasRequest = true;
            belief_.request = pendingAmps_;
            recordCommand("changed limit to " + std::to_string(pendingAmps_) + " A", pendingContext_);
        } else if (job == Job::Start) {
            belief_.code = 5;
            recordCommand("started charging", pendingContext_);
        } else if (job == Job::Stop) {
            belief_.code = 7;
            recordCommand("stopped charging", pendingContext_);
            stopOnReturn_ = false;
        } else if (job == Job::Wake) {
            belief_.asleep = false;
            nextCheckAt_ = now;   // look at the car at once: it is awake now
        }
        adopt(s, now);   // a fresh reading (the read-back, or a check) overrides the guess
        if (unreachableSince_) recovered(now);
        return;
    }

    std::string line = s.resultLines.empty() ? std::string() : s.resultLines.back();
    problem_ = line;
    if (line.find("asleep") != std::string::npos) {
        // The car answered, it is just asleep: not an outage. Look again in a couple of minutes.
        belief_.known = true;
        belief_.asleep = true;
        retryAt_ = now + 120 * 1000ULL;
        return;
    }
    if (job == Job::Wake && line.find("did not wake") != std::string::npos) return;   // heard, but still asleep: not an outage; the wake limit paces the next try
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

void CarCommander::decide(uint64_t now) {
    std::optional<int> want = wanted();
    if (!want) return;

    // A check: first look, or ten minutes since the last one.
    if (!belief_.known || now >= nextCheckAt_) {
        begin(car_.startQuietRefresh(), Job::Read, 0, now);
        return;
    }
    if (belief_.asleep) {
        if (*want > 0) maybeWake(now);
        return;
    }

    const int code = belief_.code;
    const std::string& context = fixed_ || stopOnReturn_ ? fixedContext_ : desiredContext_;
    pendingContext_ = stopOnReturn_ ? "car was out of reach" : context;

    if (*want <= 0) {
        if (charging(code)) begin(car_.startStop(), Job::Stop, 0, now);
        else if (stopOnReturn_) stopOnReturn_ = false;   // already not charging: nothing to stop
        return;
    }
    const bool same = belief_.hasRequest && belief_.request == *want;
    if (charging(code)) {
        if (!same) begin(car_.startSetAmps(*want), Job::SetAmps, *want, now);
    } else if (code == 7) {   // stopped: set the current first, then start
        if (!same) begin(car_.startSetAmps(*want), Job::SetAmps, *want, now);
        else begin(car_.startStart(), Job::Start, 0, now);
    }
    // disconnected, complete, no power or unknown: nothing to do
}

void CarCommander::maybeWake(uint64_t now) {
    if (!allowWake_) return;
    if (belief_.code == 2 || belief_.code == 6 || belief_.code == 1) return;   // unplugged, full, or never seen awake: nothing to wake it for
    if (lastWakeAt_ && now - *lastWakeAt_ < kWakeEverySeconds * 1000ULL) return;
    CarResult r = car_.startWake();
    if (!r.ok) return;
    lastWakeAt_ = now;
    pending_ = Job::Wake;
    event("Car: waking the car so charging can start");
}

void CarCommander::recordCommand(const std::string& description, const std::string& context) {
    lastCommand_ = "Car: " + description;
    lastCommandAt_ = platform_.epochSeconds();
    event(context.empty() ? lastCommand_ : lastCommand_ + ": " + context);
}

double CarCommander::currentDrawAmps() {
    if (!belief_.known || belief_.asleep || !charging(belief_.code) || !belief_.hasRequest) return 0;
    return belief_.request;   // what Brimwatt set
}

std::optional<int> CarCommander::currentLimitAmps() {
    if (!belief_.known || belief_.asleep || !charging(belief_.code)) return std::nullopt;
    return belief_.hasRequest ? belief_.request : config::kMaxAmps;
}

std::optional<ChargerStatus> CarCommander::primaryStatus() {
    if (!belief_.known) return std::nullopt;
    switch (belief_.code) {
        case 2: lastStatus_ = ChargerStatus::Available; break;
        case 3: lastStatus_ = ChargerStatus::SuspendedEVSE; break;
        case 4: case 5: case 8: lastStatus_ = ChargerStatus::Charging; break;
        case 6: lastStatus_ = ChargerStatus::Finishing; break;
        case 7: lastStatus_ = ChargerStatus::SuspendedEV; break;
        default: return std::nullopt;   // never seen awake: not known to be plugged in
    }
    return lastStatus_;
}

CarCommander::Status CarCommander::status() const {
    Status st;
    st.active = active_;
    st.problem = problem_;
    st.allowWake = allowWake_;
    if (lastWakeAt_) st.lastWakeAgeSeconds = static_cast<int>((platform_.nowMillis() - *lastWakeAt_) / 1000);
    if (unreachableSince_) st.unreachableSeconds = static_cast<int>((platform_.nowMillis() - *unreachableSince_) / 1000);
    if (!active_) st.state = "The charger is in command.";
    else if (belief_.known && belief_.asleep)
        st.state = allowWake_ ? "The car is asleep. Brimwatt will wake it when charging is wanted."
                              : "The car is asleep and waking is off: wake it with the Tesla app and Brimwatt carries on.";
    else if (unreachableSince_) st.state = "The car cannot be reached. It keeps its last setting; trying again.";
    else if (stopOnReturn_) st.state = "Stopping the charge, as you chose for when the car comes back.";
    else {
        st.state = "Brimwatt is controlling the car's charging.";
        if (belief_.known) {
            if (charging(belief_.code) && belief_.hasRequest) st.state += " Charging at " + std::to_string(belief_.request) + " A, as set by Brimwatt.";
            else if (belief_.code == 2) st.state += " The car is not plugged in.";
            else if (belief_.code == 6) st.state += " The car has finished charging.";
            else if (!charging(belief_.code)) st.state += " Not charging.";
        }
    }
    return st;
}

}  // namespace solar
