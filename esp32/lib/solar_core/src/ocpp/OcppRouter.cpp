#include "ocpp/OcppRouter.h"

#include "common/Log.h"
#include "ocpp/OcppMessages.h"

namespace solar::ocpp {

namespace {
std::string compact(JsonVariantConst value) {
    std::string out;
    serializeJson(value, out);
    return out;
}
}  // namespace

void OcppRouter::onMessage(Charger& charger, const std::string& text) {
    charger.touch(platform_.nowMillis());

    JsonDocument doc;
    if (deserializeJson(doc, text)) {
        LOG_WARN("Charger %s sent invalid JSON: %s", charger.id().c_str(), text.c_str());
        return;
    }
    JsonArrayConst frame = doc.as<JsonArrayConst>();
    if (frame.isNull() || frame.size() < 3) {
        LOG_WARN("Charger %s sent a malformed frame: %s", charger.id().c_str(), text.c_str());
        return;
    }

    int type = frame[0] | 0;
    std::string messageId = frame[1] | "";
    switch (type) {
        case kCall:
            onCall(charger, messageId, frame[2] | "", frame[3].as<JsonObjectConst>());
            break;
        case kCallResult:
            onCallResult(charger, messageId, frame[2].as<JsonObjectConst>());
            break;
        case kCallError:
            onCallError(charger, messageId, text);
            break;
        default:
            LOG_WARN("Charger %s sent unknown frame type %d: %s", charger.id().c_str(), type, text.c_str());
    }
}

void OcppRouter::onCall(Charger& charger, const std::string& messageId, const std::string& action,
                        JsonObjectConst payload) {
    if (action == "Heartbeat" || action == "MeterValues") {
        LOG_DEBUG("[OCPP] %s from charger %s: %s", action.c_str(), charger.id().c_str(), compact(payload).c_str());
    } else {
        LOG_INFO("[OCPP] %s from charger %s: %s", action.c_str(), charger.id().c_str(), compact(payload).c_str());
    }

    auto it = handlers_.find(action);
    if (it == handlers_.end()) {
        LOG_WARN("[OCPP] No handler for action %s; replying NotImplemented.", action.c_str());
        charger.send(callError(messageId, "NotImplemented", action + " is not supported"));
        return;
    }

    JsonDocument reply;
    it->second(charger, payload, reply.to<JsonObject>());
    charger.send(callResult(messageId, action, reply.as<JsonVariantConst>()));
}

void OcppRouter::onCallError(Charger& charger, const std::string& messageId, const std::string& text) {
    std::string action = charger.forgetCall(messageId);
    if (action == kMeterTrigger) {
        charger.markMeterTriggerUnsupported();
        LOG_WARN("[OCPP] Charger %s cannot report meter values on request; using the limit we set as its draw.",
                 charger.id().c_str());
        return;
    }
    LOG_ERROR("[OCPP] Charger %s rejected %s: %s", charger.id().c_str(), action.c_str(), text.c_str());
}

void OcppRouter::onCallResult(Charger& charger, const std::string& messageId, JsonObjectConst payload) {
    std::string action = charger.forgetCall(messageId);
    if (action.empty()) {
        LOG_DEBUG("[OCPP] Ignoring unmatched response %s from charger %s", messageId.c_str(), charger.id().c_str());
        return;
    }
    std::string status = payload["status"] | compact(payload).c_str();
    if (action == kMeterTrigger) {
        if (std::string(payload["status"] | "") != "Accepted") {
            charger.markMeterTriggerUnsupported();
            LOG_WARN("[OCPP] Charger %s declined a meter values request (%s); using the limit we set as its draw.",
                     charger.id().c_str(), status.c_str());
        } else {
            LOG_DEBUG("[OCPP] Charger %s accepted a meter values request.", charger.id().c_str());
        }
        return;
    }
    LOG_INFO("[OCPP] Charger %s answered %s: %s", charger.id().c_str(), action.c_str(), status.c_str());
}

}  // namespace solar::ocpp
