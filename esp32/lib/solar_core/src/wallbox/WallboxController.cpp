#include "wallbox/WallboxController.h"

#include <cmath>
#include <cstdio>

#include "common/Config.h"
#include "common/Log.h"
#include "common/Uuid.h"
#include "ocpp/OcppMessages.h"
#include "ocpp/OcppRouter.h"

namespace solar {

template <typename FrameBuilder>
void WallboxController::send(Charger& charger, const std::string& action, FrameBuilder build) {
    std::string messageId = newUuid(platform_);
    charger.rememberCall(messageId, action);
    charger.send(build(messageId));
}

void WallboxController::recordCommand(const std::string& description) {
    lastCommand_ = description;
    lastCommandAt_ = platform_.epochSeconds();
    if (history_) history_->addEvent(context_.empty() ? description : description + ": " + context_);
}

void WallboxController::sendChargingLimit(int amps) {
    chargers_.forEach([&](int, Charger& charger) { apply(charger, amps); });
}

void WallboxController::holdFixedLimit(std::optional<int> amps, const std::string& context) {
    fixedAmps_ = amps;
    fixedContext_ = context;
    if (amps) {
        context_ = context;
        sendChargingLimit(*amps);
        context_.clear();
    }
}

void WallboxController::onChargerChanged(Charger& charger) {
    if (!fixedAmps_) return;
    context_ = fixedContext_;
    apply(charger, *fixedAmps_);
    context_.clear();
}

void WallboxController::refreshMeasurements() {
    chargers_.forEach([&](int, Charger& charger) {
        if (charger.status() != ChargerStatus::Charging || charger.meterTriggerUnsupported() || !charger.isOpen()) return;
        send(charger, ocpp::kMeterTrigger, [](const std::string& id) { return ocpp::triggerMessage(id, "MeterValues"); });
    });
}

void WallboxController::requestStatus(Charger& charger) {
    if (!charger.isOpen()) return;
    send(charger, "TriggerMessage(StatusNotification)",
         [](const std::string& id) { return ocpp::triggerMessage(id, "StatusNotification"); });
    send(charger, ocpp::kMeterTrigger, [](const std::string& id) { return ocpp::triggerMessage(id, "MeterValues"); });
    LOG_INFO("[SYNC] Asked charger %s for its current status and meter values.", charger.id().c_str());
}

void WallboxController::requestStatusAll() {
    chargers_.forEach([&](int, Charger& charger) { requestStatus(charger); });
}

double WallboxController::currentDrawAmps() {
    double total = 0;
    uint64_t now = platform_.nowMillis();
    chargers_.forEach([&](int, Charger& charger) {
        if (charger.status() != ChargerStatus::Charging) return;
        total += charger.freshMeasuredAmps(now, config::kMeasurementMaxAgeMillis)
                     .value_or(charger.appliedAmps().value_or(0));
    });
    return total;
}

bool WallboxController::drawIsMeasured() {
    bool measured = false;
    uint64_t now = platform_.nowMillis();
    chargers_.forEach([&](int, Charger& charger) {
        if (charger.status() == ChargerStatus::Charging &&
            charger.freshMeasuredAmps(now, config::kMeasurementMaxAgeMillis))
            measured = true;
    });
    return measured;
}

std::optional<int> WallboxController::currentLimitAmps() {
    std::optional<int> limit;
    uint64_t now = platform_.nowMillis();
    chargers_.forEach([&](int, Charger& charger) {
        if (charger.status() != ChargerStatus::Charging) return;
        if (charger.appliedAmps()) {
            limit = charger.appliedAmps();
        } else if (auto measured = charger.freshMeasuredAmps(now, config::kMeasurementMaxAgeMillis)) {
            limit = static_cast<int>(std::ceil(*measured));
        } else {
            limit = config::kMaxAmps;
        }
    });
    return limit;
}

bool WallboxController::anyRestored() {
    bool any = false;
    chargers_.forEach([&](int, Charger& charger) { any = any || charger.isRestored(); });
    return any;
}

std::optional<ChargerStatus> WallboxController::primaryStatus() {
    std::optional<ChargerStatus> result;
    chargers_.forEach([&](int, Charger& charger) {
        if (!result || charger.status() == ChargerStatus::Charging) result = charger.status();
    });
    return result;
}

std::string WallboxController::describe() {
    if (chargers_.empty()) return "no charger connected";
    std::string text;
    uint64_t now = platform_.nowMillis();
    chargers_.forEach([&](int, Charger& c) {
        char line[200];
        auto measured = c.freshMeasuredAmps(now, config::kMeasurementMaxAgeMillis);
        char draw[32] = "n/a";
        if (measured) std::snprintf(draw, sizeof draw, "%.1f A measured", *measured);
        std::snprintf(line, sizeof line, "%s: %s, transaction %s, limit %s, draw %s", c.id().c_str(), toString(c.status()),
                      c.transactionId() ? std::to_string(*c.transactionId()).c_str() : "none",
                      c.appliedAmps() ? (std::to_string(*c.appliedAmps()) + " A").c_str() : "none", draw);
        if (!text.empty()) text += "; ";
        text += line;
    });
    return text;
}

void WallboxController::apply(Charger& charger, int amps) {
    if (!charger.isOpen()) return;
    if (amps <= 0) {
        stop(charger);
    } else if (charger.status() == ChargerStatus::Charging) {
        updateLimit(charger, amps);
    } else if (canStartCharging(charger.status())) {
        start(charger, amps);
    } else {
        LOG_DEBUG("Charger %s is %s; nothing to apply.", charger.id().c_str(), toString(charger.status()));
    }
}

void WallboxController::start(Charger& charger, int amps) {
    uint64_t now = platform_.nowMillis();
    auto last = charger.lastRemoteStartMillis();
    if (last && now - *last < config::kRemoteStartRetryMillis) return;

    send(charger, "RemoteStartTransaction", [&](const std::string& id) {
        return ocpp::remoteStartTransaction(id, newUuid(platform_), static_cast<int>(platform_.random32() & 0x7fffffff),
                                            amps, config::kChargingRateUnit);
    });
    charger.markRemoteStart(now);
    charger.setAppliedAmps(amps);
    recordCommand("Started charging at " + std::to_string(amps) + " A");
    LOG_INFO("[DISPATCH] RemoteStartTransaction (%dA) -> charger %s", amps, charger.id().c_str());
}

void WallboxController::updateLimit(Charger& charger, int amps) {
    auto transactionId = charger.transactionId();
    if (!transactionId) {
        LOG_WARN("Charger %s is charging but no transaction is known; cannot set a limit.", charger.id().c_str());
        return;
    }
    if (charger.appliedAmps() == amps) return;

    send(charger, "SetChargingProfile", [&](const std::string& id) {
        return ocpp::setChargingProfile(id, *transactionId, amps, config::kChargingRateUnit);
    });
    charger.setAppliedAmps(amps);
    recordCommand("Changed limit to " + std::to_string(amps) + " A");
    LOG_INFO("[DISPATCH] SetChargingProfile (%dA, txn %d) -> charger %s", amps, *transactionId, charger.id().c_str());
}

void WallboxController::stop(Charger& charger) {
    if (charger.status() != ChargerStatus::Charging) return;
    auto transactionId = charger.transactionId();
    if (!transactionId) {
        LOG_WARN("Charger %s is charging but no transaction is known; cannot stop it. Waiting for it to report one.",
                 charger.id().c_str());
        return;
    }
    send(charger, "RemoteStopTransaction",
         [&](const std::string& id) { return ocpp::remoteStopTransaction(id, *transactionId); });
    recordCommand("Stopped charging");
    LOG_INFO("[DISPATCH] RemoteStopTransaction (txn %d) -> charger %s", *transactionId, charger.id().c_str());
}

}  // namespace solar
