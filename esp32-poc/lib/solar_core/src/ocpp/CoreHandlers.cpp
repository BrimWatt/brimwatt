#include "ocpp/CoreHandlers.h"

#include <algorithm>
#include <cstdlib>
#include <memory>

#include "common/Config.h"
#include "common/Log.h"
#include "ocpp/IdTagHash.h"

namespace solar::ocpp {

namespace {

double sampleValue(JsonVariantConst value) {
    if (value.is<const char*>()) return std::strtod(value.as<const char*>(), nullptr);
    return value.as<double>();
}

/** Remembers the current the charger reports delivering (Current.Import, or Power.Active.Import / voltage). */
/** @return true if this message taught us the transaction id */
bool recordMeasuredCurrent(Charger& charger, JsonObjectConst payload, uint64_t now) {
    double amps = -1;
    double watts = -1;
    for (JsonObjectConst meterValue : payload["meterValue"].as<JsonArrayConst>()) {
        for (JsonObjectConst sample : meterValue["sampledValue"].as<JsonArrayConst>()) {
            std::string measurand = sample["measurand"] | "";
            std::string unit = sample["unit"] | "";
            double value = sampleValue(sample["value"]);
            if (measurand == "Current.Import") {
                amps = std::max(amps, value);
            } else if (measurand == "Power.Active.Import") {
                watts = std::max(watts, unit == "kW" ? value * 1000 : value);
            }
        }
    }
    // A transaction id here lets us control a charge that started before we (re)connected.
    bool learnedTransaction = false;
    if (!payload["transactionId"].isNull() && !charger.transactionId()) {
        charger.setTransactionId(payload["transactionId"].as<int>());
        learnedTransaction = true;
        LOG_INFO("Charger %s reports transaction %d", charger.id().c_str(), *charger.transactionId());
    }
    if (amps >= 0) {
        charger.setMeasuredAmps(amps, now);
        if (amps >= 1 && charger.status() == ChargerStatus::Unknown) {
            charger.setStatus(ChargerStatus::Charging);   // it is delivering current, so it is charging
        }
    } else if (watts >= 0) {
        charger.setMeasuredAmps(watts / config::kHouseholdVoltage, now);
    }
    return learnedTransaction;
}

}  // namespace

void registerCoreHandlers(OcppRouter& router, Platform& platform, std::function<void(Charger&)> onChargerChanged,
                          HistoryService* history) {
    // Seed from the clock so transaction ids are not reused after a restart.
    auto nextTransactionId = std::make_shared<int>(static_cast<int>(platform.epochSeconds()));

    router.on("BootNotification", [&platform](Charger&, JsonObjectConst, JsonObject reply) {
        reply["status"] = "Accepted";
        reply["currentTime"] = platform.utcIso();
        reply["interval"] = config::kHeartbeatIntervalSeconds;
    });

    router.on("Heartbeat", [&platform](Charger&, JsonObjectConst, JsonObject reply) {
        reply["currentTime"] = platform.utcIso();
    });

    router.on("StatusNotification", [onChargerChanged, history](Charger& c, JsonObjectConst p, JsonObject) {
        // Connector 0 describes the whole charge point, not the plug.
        int connector = p["connectorId"] | 1;
        if (connector > 0) {
            ChargerStatus status = parseChargerStatus(p["status"] | "");
            bool wasRestored = c.isRestored();
            ChargerStatus before = c.status();
            c.setStatus(status);
            if (history) {
                if (status == ChargerStatus::Preparing && before == ChargerStatus::Available) {
                    history->addEvent("Car plugged in");
                    history->notePluggedIn();
                }
                if (status == ChargerStatus::Available && before != ChargerStatus::Available &&
                    before != ChargerStatus::Unknown) {
                    history->addEvent("Car unplugged");
                    history->notePluggedOut();
                }
            }
            // No transaction exists in these states, so a restored one must be stale.
            if (wasRestored && (status == ChargerStatus::Available || status == ChargerStatus::Preparing ||
                                status == ChargerStatus::Finishing)) {
                c.setTransactionId(std::nullopt);
                c.setAppliedAmps(std::nullopt);
            }
            LOG_INFO("Charger %s status: %s", c.id().c_str(), toString(status));
            onChargerChanged(c);
        }
    });

    router.on("Authorize", [](Charger&, JsonObjectConst, JsonObject reply) {
        reply["idTagInfo"]["status"] = "Accepted";
    });

    router.on("StartTransaction", [nextTransactionId, onChargerChanged, history](Charger& c, JsonObjectConst p, JsonObject reply) {
        int transactionId = ++*nextTransactionId;
        c.setTransactionId(transactionId);
        LOG_INFO("Charger %s started transaction %d", c.id().c_str(), transactionId);
        if (history) history->sessionStarted(p["meterStart"].isNull() ? -1.0 : p["meterStart"].as<double>(), idTagHash(std::string(p["idTag"] | "")));   // only a fingerprint is kept
        onChargerChanged(c);
        reply["transactionId"] = transactionId;
        reply["idTagInfo"]["status"] = "Accepted";
    });

    router.on("StopTransaction", [history](Charger& c, JsonObjectConst p, JsonObject reply) {
        LOG_INFO("Charger %s stopped transaction %d", c.id().c_str(), c.transactionId().value_or(-1));
        if (history) {
            history->sessionStopped(p["meterStop"].isNull() ? -1.0 : p["meterStop"].as<double>(), std::string(p["reason"] | ""));
        }
        c.setTransactionId(std::nullopt);
        c.setAppliedAmps(std::nullopt);
        reply["idTagInfo"]["status"] = "Accepted";
    });

    router.on("MeterValues", [&platform, onChargerChanged](Charger& c, JsonObjectConst p, JsonObject) {
        // A charge found running after a restart becomes controllable once its transaction id is known, so let
        // whoever holds a fixed current (Custom mode) apply it now.
        if (recordMeasuredCurrent(c, p, platform.nowMillis())) onChargerChanged(c);
    });
}

}  // namespace solar::ocpp
