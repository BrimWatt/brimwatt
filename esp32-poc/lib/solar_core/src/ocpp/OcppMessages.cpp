#include "ocpp/OcppMessages.h"

#include "common/Config.h"

namespace solar::ocpp {

namespace {

std::string serialize(const JsonDocument& doc) {
    std::string out;
    serializeJson(doc, out);
    return out;
}

void fillChargingProfile(JsonObject profile, int profileId, int amps, const char* unit) {
    profile["chargingProfileId"] = profileId;
    profile["stackLevel"] = config::kStackLevel;
    profile["chargingProfileKind"] = "Absolute";
    profile["chargingProfilePurpose"] = "TxProfile";
    JsonObject schedule = profile["chargingSchedule"].to<JsonObject>();
    schedule["chargingRateUnit"] = unit;
    schedule["minChargingRate"] = amps;
    JsonObject period = schedule["chargingSchedulePeriod"].to<JsonArray>().add<JsonObject>();
    period["limit"] = amps;
    period["startPeriod"] = 0;
}

}  // namespace

std::string callResult(const std::string& messageId, const std::string& action, JsonVariantConst payload) {
    JsonDocument doc;
    JsonArray frame = doc.to<JsonArray>();
    frame.add(kCallResult);
    frame.add(messageId);
    frame.add(action);
    frame.add(payload);
    return serialize(doc);
}

std::string callError(const std::string& messageId, const std::string& code, const std::string& description) {
    JsonDocument doc;
    JsonArray frame = doc.to<JsonArray>();
    frame.add(kCallError);
    frame.add(messageId);
    frame.add(code);
    frame.add(description);
    frame.add<JsonObject>();
    return serialize(doc);
}

std::string setChargingProfile(const std::string& messageId, int transactionId, int amps, const char* unit) {
    JsonDocument doc;
    JsonArray frame = doc.to<JsonArray>();
    frame.add(kCall);
    frame.add(messageId);
    frame.add("SetChargingProfile");
    JsonObject payload = frame.add<JsonObject>();
    payload["connectorId"] = config::kConnectorId;
    JsonObject profile = payload["csChargingProfiles"].to<JsonObject>();
    fillChargingProfile(profile, 0, amps, unit);
    profile["transactionId"] = transactionId;
    return serialize(doc);
}

std::string remoteStartTransaction(const std::string& messageId, const std::string& idTag, int profileId, int amps,
                                   const char* unit) {
    JsonDocument doc;
    JsonArray frame = doc.to<JsonArray>();
    frame.add(kCall);
    frame.add(messageId);
    frame.add("RemoteStartTransaction");
    JsonObject payload = frame.add<JsonObject>();
    payload["idTag"] = idTag;
    payload["connectorId"] = config::kConnectorId;
    fillChargingProfile(payload["chargingProfile"].to<JsonObject>(), profileId, amps, unit);
    return serialize(doc);
}

std::string remoteStopTransaction(const std::string& messageId, int transactionId) {
    JsonDocument doc;
    JsonArray frame = doc.to<JsonArray>();
    frame.add(kCall);
    frame.add(messageId);
    frame.add("RemoteStopTransaction");
    frame.add<JsonObject>()["transactionId"] = transactionId;
    return serialize(doc);
}

std::string triggerMessage(const std::string& messageId, const char* requestedMessage) {
    JsonDocument doc;
    JsonArray frame = doc.to<JsonArray>();
    frame.add(kCall);
    frame.add(messageId);
    frame.add("TriggerMessage");
    JsonObject payload = frame.add<JsonObject>();
    payload["requestedMessage"] = requestedMessage;
    payload["connectorId"] = config::kConnectorId;
    return serialize(doc);
}

}  // namespace solar::ocpp
