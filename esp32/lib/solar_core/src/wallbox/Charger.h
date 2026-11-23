#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace solar {

/** Connector states from the OCPP 1.6 StatusNotification. */
enum class ChargerStatus {
    Available, Preparing, Charging, SuspendedEV, SuspendedEVSE, Finishing, Reserved, Unavailable, Faulted, Unknown
};

const char* toString(ChargerStatus status);
ChargerStatus parseChargerStatus(const std::string& text);

/** States in which a remote start can begin a charge. */
bool canStartCharging(ChargerStatus status);

/** What we last knew about a charger, saved so it survives a restart of the board. */
struct ChargerSnapshot {
    ChargerStatus status = ChargerStatus::Unknown;
    std::optional<int> transactionId;
    std::optional<int> appliedAmps;
    int64_t savedAtSeconds = 0;   // wall-clock time (epoch seconds)

    /** True if everything except the timestamp matches. */
    bool sameStateAs(const ChargerSnapshot& other) const {
        return status == other.status && transactionId == other.transactionId && appliedAmps == other.appliedAmps;
    }
};

/** How a charger is reached. The firmware backs this with a WebSocket client; tests record what is sent. */
class ChargerLink {
public:
    virtual ~ChargerLink() = default;
    virtual bool isOpen() const = 0;
    virtual void send(const std::string& frame) = 0;
};

/** A connected charger: its link plus what we know about its connector and transaction. */
class Charger {
public:
    Charger(std::string id, ChargerLink& link, uint64_t connectedAtMillis)
        : id_(std::move(id)), link_(link), lastSeenMillis_(connectedAtMillis) {}

    const std::string& id() const { return id_; }

    ChargerStatus status() const { return status_; }

    /** A status reported live by the charger also confirms anything restored from the saved state. */
    void setStatus(ChargerStatus status) {
        status_ = status;
        restored_ = false;
    }

    /** True while the state comes from the saved copy and the charger has not confirmed it yet. */
    bool isRestored() const { return restored_; }

    ChargerSnapshot snapshot(int64_t nowSeconds) const { return {status_, transactionId_, appliedAmps_, nowSeconds}; }

    /** Adopts the last known state after a restart, until the charger reports for itself. */
    void restoreFrom(const ChargerSnapshot& snapshot) {
        status_ = snapshot.status;
        transactionId_ = snapshot.transactionId;
        appliedAmps_ = snapshot.appliedAmps;
        restored_ = true;
    }

    std::optional<int> transactionId() const { return transactionId_; }
    void setTransactionId(std::optional<int> id) { transactionId_ = id; }

    /** Last current limit we asked the charger to use, if one is in effect. */
    std::optional<int> appliedAmps() const { return appliedAmps_; }
    void setAppliedAmps(std::optional<int> amps) { appliedAmps_ = amps; }

    /** The charger's own reading of the current it delivers, from MeterValues. */
    void setMeasuredAmps(double amps, uint64_t nowMillis) {
        measuredAmps_ = amps;
        measuredAtMillis_ = nowMillis;
    }
    std::optional<double> freshMeasuredAmps(uint64_t nowMillis, uint64_t maxAgeMillis) const {
        if (!measuredAtMillis_ || nowMillis - *measuredAtMillis_ > maxAgeMillis) return std::nullopt;
        return measuredAmps_;
    }

    /** Set once a charger has refused a request for meter values, so we stop asking. */
    bool meterTriggerUnsupported() const { return meterTriggerUnsupported_; }
    void markMeterTriggerUnsupported() { meterTriggerUnsupported_ = true; }

    std::optional<uint64_t> lastRemoteStartMillis() const { return lastRemoteStartMillis_; }
    void markRemoteStart(uint64_t nowMillis) { lastRemoteStartMillis_ = nowMillis; }

    /** Any message from the charger counts as a sign of life. */
    void touch(uint64_t nowMillis) { lastSeenMillis_ = nowMillis; }
    uint64_t lastSeenMillis() const { return lastSeenMillis_; }

    bool isOpen() const { return link_.isOpen(); }
    void send(const std::string& frame) { link_.send(frame); }

    /** Remembers what a call was, so its answer (or refusal) can be matched to it. */
    void rememberCall(const std::string& messageId, const std::string& action) { pendingCalls_[messageId] = action; }
    std::string forgetCall(const std::string& messageId) {
        auto it = pendingCalls_.find(messageId);
        if (it == pendingCalls_.end()) return "";
        std::string action = it->second;
        pendingCalls_.erase(it);
        return action;
    }

private:
    std::string id_;
    ChargerLink& link_;
    ChargerStatus status_ = ChargerStatus::Unknown;
    std::optional<int> transactionId_;
    std::optional<int> appliedAmps_;
    std::optional<double> measuredAmps_;
    std::optional<uint64_t> measuredAtMillis_;
    std::optional<uint64_t> lastRemoteStartMillis_;
    bool meterTriggerUnsupported_ = false;
    bool restored_ = false;
    uint64_t lastSeenMillis_;
    std::map<std::string, std::string> pendingCalls_;
};

}  // namespace solar
