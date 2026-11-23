#pragma once

#include <ArduinoJson.h>

#include <string>

namespace solar::ocpp {

constexpr int kCall = 2;
constexpr int kCallResult = 3;
constexpr int kCallError = 4;

// ---- replies to the charger ----

/**
 * The reply layout is [3, id, action, payload], copied from the earlier WattSync project that is known to work
 * with this wallbox. The OCPP specification shows [3, id, payload]; change it here if a charger needs that.
 */
std::string callResult(const std::string& messageId, const std::string& action, JsonVariantConst payload);

std::string callError(const std::string& messageId, const std::string& code, const std::string& description);

// ---- commands to the charger ----

std::string setChargingProfile(const std::string& messageId, int transactionId, int amps, const char* unit);

std::string remoteStartTransaction(const std::string& messageId, const std::string& idTag, int profileId, int amps,
                                   const char* unit);

std::string remoteStopTransaction(const std::string& messageId, int transactionId);

/** Asks the charger to send a message it would normally send on its own (StatusNotification, MeterValues). */
std::string triggerMessage(const std::string& messageId, const char* requestedMessage);

}  // namespace solar::ocpp
