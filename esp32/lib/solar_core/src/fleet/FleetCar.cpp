#include "fleet/FleetCar.h"

#include <memory>

#include "tesla/TeslaBle.h"

namespace solar {
namespace fleet {

namespace {
using tesla::Bytes;

/** Hands a flow's messages to the car through signed_command and the car's answers back to the flow, until the flow is done. */
template <class Flow>
bool drive(Flow& flow, FleetApi& api, const std::string& vin, Platform& platform, std::string& error) {
    Bytes message = flow.start();
    for (int step = 0; step < 12 && !flow.finished(); step++) {
        if (message.empty()) break;
        Bytes reply;
        if (!api.signedCommand(vin, message, reply, error)) {
            flow.timedOut();
            return false;
        }
        message = flow.onMessage(reply, static_cast<uint32_t>(platform.nowMillis()));
    }
    if (!flow.finished()) flow.timedOut();
    return flow.finished();
}
}  // namespace

bool FleetCar::ready(std::string& why) const {
    if (!auth_.connected()) { why = "Not connected to a Tesla account yet. Sign in first."; return false; }
    if (identity_.vin().empty()) { why = "No car set yet."; return false; }
    Bytes key;
    if (!identity_.privateKey(key)) { why = "No signing key on this board yet."; return false; }
    return true;
}

CarResult FleetCar::refuse(const std::string& message, bool busy) const {
    CarResult r;
    r.ok = false;
    r.busy = busy;
    r.message = message;
    return r;
}

CarSnapshot FleetCar::snapshot() const {
    CarSnapshot s;
    s.supported = true;
    std::string why;
    s.paired = ready(why);
    s.watch = watch_;
    if (s.paired) {
        s.vin = tesla::maskVin(identity_.vin());
        Bytes priv, pub;
        if (identity_.privateKey(priv)) {
            std::lock_guard<std::mutex> lock(m_);
            if (priv != fingerprintFor_) {   // deriving the public key is slow on the board: only when the key changed
                fingerprintFor_ = priv;
                fingerprint_.clear();
                if (crypto_.publicKeyFor(priv, pub)) {
                    static const char* hex = "0123456789abcdef";
                    const Bytes h = crypto_.sha256(pub);
                    for (int i = 0; i < 4 && i < static_cast<int>(h.size()); i++) { fingerprint_ += hex[h[i] >> 4]; fingerprint_ += hex[h[i] & 15]; }
                }
            }
            s.key = fingerprint_;
        }
    }
    s.busy = running_.load();
    std::lock_guard<std::mutex> lock(m_);
    s.job = s.busy ? job_ : "";
    if (result_.have) {
        s.hasResult = true;
        s.resultJob = result_.job;
        s.resultOk = result_.ok;
        s.resultAgeSeconds = static_cast<int>((platform_.nowMillis() - result_.atMs) / 1000);
        s.resultLines = result_.lines;
    }
    s.resultSerial = serial_;
    if (reading_.have && s.paired) {
        s.hasReading = true;
        s.readingAgeSeconds = static_cast<int>((platform_.nowMillis() - reading_.atMs) / 1000);
        s.sleep = reading_.sleep;
        s.lock = -1;
        s.charge = reading_.charge;
    }
    s.restoreAmps = restoreAmps_;
    return s;
}

CarResult FleetCar::startPair(const std::string&) {
    return refuse("In Tesla account mode the car is added through Tesla's key link, not by pairing here.");
}

CarResult FleetCar::launch(const std::string& job, std::function<void()> work) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return refuse("The car is busy with another job.", true);
    {
        std::lock_guard<std::mutex> lock(m_);
        job_ = job;
    }
    if (!executor_.start(std::move(work))) {
        running_ = false;
        return refuse("Not enough memory to start the job. Try again in a moment.");
    }
    return CarResult();
}

void FleetCar::finish(const std::string& job, bool ok, const std::vector<std::string>& lines, bool quiet) {
    {
        std::lock_guard<std::mutex> lock(m_);
        // A quiet read that works leaves no trace in the result lines; a failing one still does, once per distinct problem.
        const bool skip = quiet && ok;
        const bool repeat = quiet && !ok && !lines.empty() && lines[0] == lastQuietError_;
        if (!skip && !repeat) {
            result_.have = true;
            result_.job = job;
            result_.ok = ok;
            result_.atMs = platform_.nowMillis();
            result_.lines = lines;
        }
        lastQuietError_ = (quiet && !ok && !lines.empty()) ? lines[0] : "";
        serial_++;
        nextReadAt_ = platform_.nowMillis() + static_cast<uint64_t>(kWatchEverySeconds) * 1000;
        nextReadSet_ = true;
    }
    running_ = false;
}

void FleetCar::noteReading(const FleetRead& read) {
    std::lock_guard<std::mutex> lock(m_);
    reading_.have = true;
    reading_.atMs = platform_.nowMillis();
    reading_.sleep = read.asleep ? 2 : 1;
    if (read.ok) reading_.charge = read.charge;
}

CarResult FleetCar::startRead(bool quiet) {
    std::string why;
    if (!ready(why)) return refuse(why);
    const std::string vin = identity_.vin();
    return launch("status", [this, vin, quiet]() {
        const FleetRead read = api_.readCharge(vin);
        std::vector<std::string> lines;
        if (read.ok || read.asleep) noteReading(read);
        if (read.ok) tesla::describeCharge(read.charge, lines);
        else if (read.asleep) lines.push_back(read.error);
        else lines.push_back(read.error);
        finish("status", read.ok || read.asleep, lines, quiet);
    });
}

CarResult FleetCar::startCharge(tesla::ChargeCommandFlow::Command command, int amps) {
    using tesla::ChargeCommandFlow;
    if (command == ChargeCommandFlow::SetAmps && (amps < ChargeCommandFlow::kMinAmps || amps > ChargeCommandFlow::kMaxAmps))
        return refuse("The current must be between 6 and 40 A.");
    std::string why;
    if (!ready(why)) return refuse(why);
    const std::string vin = identity_.vin();
    Bytes priv, pub;
    identity_.privateKey(priv);
    if (!crypto_.publicKeyFor(priv, pub)) return refuse("The signing key on this board is not usable.");
    if (command == ChargeCommandFlow::SetAmps) {
        std::lock_guard<std::mutex> lock(m_);
        if (restoreAmps_ == 0 && reading_.have && reading_.charge.valid && reading_.charge.hasCurrentRequest && reading_.charge.currentRequest > 0)
            restoreAmps_ = reading_.charge.currentRequest;
        pendingAmps_ = amps;
    }
    const CarResult started = launch("charge", [this, vin, priv, pub, command, amps]() {
        std::unique_ptr<ChargeCommandFlow> flow(new ChargeCommandFlow(crypto_, priv, pub, vin, command, amps));
        std::string error;
        const bool done = drive(*flow, api_, vin, platform_, error);
        std::vector<std::string> lines = flow->lines();
        if (!done && !error.empty()) lines.insert(lines.begin(), error);
        const bool ok = done && flow->ok();
        if (flow->charge().valid) {
            std::lock_guard<std::mutex> lock(m_);
            reading_.have = true;
            reading_.atMs = platform_.nowMillis();
            reading_.sleep = 1;
            reading_.charge = flow->charge();
        }
        {
            std::lock_guard<std::mutex> lock(m_);
            if (ok && pendingAmps_ != 0 && pendingAmps_ == restoreAmps_) restoreAmps_ = 0;   // back to where it was
            pendingAmps_ = 0;
        }
        finish("charge", ok, lines, false);
    });
    if (!started.ok) {
        std::lock_guard<std::mutex> lock(m_);
        pendingAmps_ = 0;
    }
    return started;
}

CarResult FleetCar::startWake() {
    std::string why;
    if (!ready(why)) return refuse(why);
    const std::string vin = identity_.vin();
    return launch("wake", [this, vin]() {
        std::vector<std::string> lines;
        FleetRead look = api_.readCharge(vin);
        if (look.ok) {   // already awake: leave it alone
            noteReading(look);
            lines.push_back("The car is already awake.");
            finish("wake", true, lines, false);
            return;
        }
        if (!look.asleep) {
            lines.push_back(look.error);
            finish("wake", false, lines, false);
            return;
        }
        std::string error;
        if (!api_.wake(vin, error)) {
            lines.push_back(error);
            finish("wake", false, lines, false);
            return;
        }
        for (int i = 0; i < kWakeLooks; i++) {
            executor_.sleepMs(kWakeLookEveryMs);
            look = api_.readCharge(vin);
            if (look.ok) {
                noteReading(look);
                lines.push_back("The car is awake.");
                finish("wake", true, lines, false);
                return;
            }
        }
        lines.push_back("Asked the car to wake, but it has not answered yet.");
        finish("wake", false, lines, false);
    });
}

void FleetCar::forget() {
    auth_.forget();
    std::lock_guard<std::mutex> lock(m_);
    reading_ = Reading();
    result_ = Result();
    restoreAmps_ = 0;
    pendingAmps_ = 0;
    nextReadSet_ = false;
}

void FleetCar::loop() {
    if (!watch_ || controlled_ || running_.load()) return;
    std::string why;
    if (!ready(why)) return;
    const uint64_t now = platform_.nowMillis();
    if (!nextReadSet_) {
        nextReadAt_ = now + 60000;
        nextReadSet_ = true;
    }
    if (static_cast<int64_t>(now - nextReadAt_) < 0) return;
    nextReadAt_ = now + static_cast<uint64_t>(kWatchEverySeconds) * 1000;   // also the retry time
    startRead(true);
}

}  // namespace fleet
}  // namespace solar
