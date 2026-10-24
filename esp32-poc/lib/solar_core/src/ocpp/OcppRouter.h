#pragma once

#include <ArduinoJson.h>

#include <functional>
#include <map>
#include <string>

#include "common/Platform.h"
#include "wallbox/Charger.h"

namespace solar::ocpp {

/** Name under which a MeterValues request is remembered, so a refusal can be matched to it. */
constexpr const char* kMeterTrigger = "TriggerMessage(MeterValues)";

/** Handles one action the charger initiates; fills `reply` (already an empty object) with the reply payload. */
using OcppHandler = std::function<void(Charger& charger, JsonObjectConst payload, JsonObject reply)>;

/** Parses incoming OCPP frames and dispatches them. */
class OcppRouter {
public:
    explicit OcppRouter(const Platform& platform) : platform_(platform) {}

    void on(const std::string& action, OcppHandler handler) { handlers_[action] = std::move(handler); }

    void onMessage(Charger& charger, const std::string& text);

private:
    void onCall(Charger& charger, const std::string& messageId, const std::string& action, JsonObjectConst payload);
    void onCallResult(Charger& charger, const std::string& messageId, JsonObjectConst payload);
    void onCallError(Charger& charger, const std::string& messageId, const std::string& text);

    const Platform& platform_;
    std::map<std::string, OcppHandler> handlers_;
};

}  // namespace solar::ocpp
