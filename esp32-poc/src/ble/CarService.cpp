#include "ble/CarService.h"

#include "tesla/TeslaBle.h"

namespace device {

using solar::CarResult;
using solar::CarSnapshot;

namespace {
int ageSeconds(uint32_t atMs) { return static_cast<int>((millis() - atMs) / 1000); }
}  // namespace

void CarService::loop() {
    car_.loop();

    // A job just finished: settle the "put the setting back" note, and wait before the next watch read.
    const TeslaCar::Result& result = car_.lastResult();
    if (result.have && (!seenResult_ || result.atMs != seenResultAt_)) {
        seenResult_ = true;
        seenResultAt_ = result.atMs;
        if (std::string(result.job) == "charge" && result.ok && pendingAmps_ != 0 && pendingAmps_ == restoreAmps_) restoreAmps_ = 0;   // back to where it was
        pendingAmps_ = 0;
        nextReadAt_ = millis() + kWatchEveryMillis;
        nextReadSet_ = true;
    }

    if (!watch_ || !car_.paired() || car_.busy() || scan_.running()) return;
    if (!nextReadSet_) {
        nextReadAt_ = millis() + kFirstReadMillis;
        nextReadSet_ = true;
    }
    if (static_cast<int32_t>(millis() - nextReadAt_) < 0) return;
    nextReadAt_ = millis() + kWatchEveryMillis;   // also the retry time if nobody answers
    car_.startStatus(/*quiet=*/true);
}

CarResult CarService::blockedBy() const {
    CarResult r;
    if (car_.busy()) {
        r.ok = false;
        r.busy = true;
        r.message = "The car is busy with another job. Wait for it, or stop it first.";
    } else if (scan_.running()) {
        r.ok = false;
        r.busy = true;
        r.message = "A Bluetooth scan is running. Type \"tesla stop\" in the console first.";
    }
    return r;
}

CarResult CarService::started(const std::string& error) {
    CarResult r;
    if (!error.empty()) {
        r.ok = false;
        r.message = error;
    }
    return r;
}

CarSnapshot CarService::snapshot() const {
    CarSnapshot s;
    s.supported = true;
    s.paired = const_cast<TeslaCar&>(car_).paired();
    s.watch = watch_;
    if (s.paired) {
        s.vin = solar::tesla::maskVin(store_.loadVin());
        s.key = const_cast<TeslaCar&>(car_).keyFingerprint();
    }
    s.busy = car_.busy();
    s.job = car_.jobName();
    if (s.busy && s.job == "pair") {
        s.pairState = car_.pairState();
        s.pairText = car_.pairText();
        s.pairSecondsLeft = car_.pairSecondsLeft();
    }
    const TeslaCar::Result& result = car_.lastResult();
    if (result.have) {
        s.hasResult = true;
        s.resultJob = result.job;
        s.resultOk = result.ok;
        s.resultAgeSeconds = ageSeconds(result.atMs);
        s.resultLines = result.lines;
    }
    const TeslaCar::Reading& reading = car_.lastReading();
    if (reading.have && s.paired) {
        s.hasReading = true;
        s.readingAgeSeconds = ageSeconds(reading.atMs);
        s.sleep = reading.sleep;
        s.lock = reading.lock;
        s.charge = reading.charge;
    }
    s.restoreAmps = restoreAmps_;
    return s;
}

CarResult CarService::startPair(const std::string& vin) {
    CarResult blocked = blockedBy();
    if (!blocked.ok) return blocked;
    return started(car_.startPair(vin));
}

CarResult CarService::startRefresh() {
    CarResult blocked = blockedBy();
    if (!blocked.ok) return blocked;
    return started(car_.startStatus(/*quiet=*/false));
}

CarResult CarService::startStart() {
    CarResult blocked = blockedBy();
    if (!blocked.ok) return blocked;
    return started(car_.startCharge(solar::tesla::ChargeCommandFlow::Start, 0));
}

CarResult CarService::startStop() {
    CarResult blocked = blockedBy();
    if (!blocked.ok) return blocked;
    return started(car_.startCharge(solar::tesla::ChargeCommandFlow::Stop, 0));
}

void CarService::rememberRestore() {
    const TeslaCar::Reading& reading = car_.lastReading();
    if (restoreAmps_ == 0 && reading.have && reading.charge.valid && reading.charge.hasCurrentRequest && reading.charge.currentRequest > 0) {
        restoreAmps_ = reading.charge.currentRequest;
    }
}

CarResult CarService::startSetAmps(int amps) {
    CarResult blocked = blockedBy();
    if (!blocked.ok) return blocked;
    rememberRestore();
    pendingAmps_ = amps;
    CarResult r = started(car_.startCharge(solar::tesla::ChargeCommandFlow::SetAmps, amps));
    if (!r.ok) pendingAmps_ = 0;
    return r;
}

void CarService::setWatch(bool on) {
    watch_ = on;
    store_.saveWatch(on);
    nextReadSet_ = false;   // the next read comes a minute after switching on
}

void CarService::forget() {
    car_.forget();
    restoreAmps_ = 0;
    pendingAmps_ = 0;
    nextReadSet_ = false;
}

}  // namespace device
