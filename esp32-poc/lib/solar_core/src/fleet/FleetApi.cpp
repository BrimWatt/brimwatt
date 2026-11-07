#include "fleet/FleetApi.h"

#include <ArduinoJson.h>

#include "fleet/Base64.h"

namespace solar {
namespace fleet {

int FleetApi::chargingStateCode(const std::string& text) {
    if (text == "Disconnected") return 2;
    if (text == "NoPower") return 3;
    if (text == "Starting") return 4;
    if (text == "Charging") return 5;
    if (text == "Complete") return 6;
    if (text == "Stopped") return 7;
    if (text == "Calibrating") return 8;
    return 1;
}

bool FleetApi::parseChargeState(const std::string& json, tesla::ChargeState& out) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;
    JsonObjectConst s = doc["response"]["charge_state"];
    if (s.isNull()) s = doc["response"]["response"]["charge_state"];   // some replies nest one level deeper
    if (s.isNull()) return false;
    tesla::ChargeState c;
    c.valid = true;
    c.chargingState = chargingStateCode(s["charging_state"] | "");
    auto num = [&](const char* key, bool& has, int& value) {
        if (s[key].is<int>()) { has = true; value = s[key].as<int>(); }
        else if (s[key].is<double>()) { has = true; value = static_cast<int>(s[key].as<double>() + 0.5); }
    };
    num("battery_level", c.hasBatteryLevel, c.batteryLevel);
    num("charge_limit_soc", c.hasChargeLimit, c.chargeLimit);
    num("charger_actual_current", c.hasActualCurrent, c.actualCurrent);
    num("charge_current_request", c.hasCurrentRequest, c.currentRequest);
    num("charge_current_request_max", c.hasCurrentRequestMax, c.currentRequestMax);
    num("charger_power", c.hasPower, c.power);
    num("charger_voltage", c.hasVoltage, c.voltage);
    out = c;
    return true;
}

std::string FleetApi::describeStatus(int status) {
    switch (status) {
        case 401: return "Tesla refused the sign-in. Sign in again.";
        case 402: return "Tesla says the app has no billing set up or has used its limit.";
        case 403: return "Tesla does not allow this app to do that (check the app's permissions and the car's key).";
        case 404: return "Tesla does not know this car for this account.";
        case 408: return "The car is asleep or offline.";
        case 429: return "Tesla says too many requests. Waiting.";
        default: break;
    }
    if (status >= 500) return "Tesla had a problem (code " + std::to_string(status) + ").";
    return "Tesla answered with code " + std::to_string(status) + ".";
}

FleetHttpResponse FleetApi::call(const std::string& method, const std::string& path, const std::string& body, std::string& error) {
    for (int attempt = 0; attempt < 2; attempt++) {
        const std::string token = auth_.accessToken();
        if (token.empty()) {
            error = auth_.error().empty() ? "Not connected to a Tesla account yet." : auth_.error();
            return FleetHttpResponse();   // sent = false
        }
        FleetHttpRequest req;
        req.method = method;
        req.url = auth_.apiBase() + path;
        req.body = body;
        req.headers.push_back({"Authorization", "Bearer " + token});
        if (!body.empty()) req.headers.push_back({"Content-Type", "application/json"});
        FleetHttpResponse res = http_.send(req);
        if (!res.sent) {
            error = "Could not reach Tesla (no internet?).";
            return res;
        }
        if (res.status == 401 && attempt == 0) {
            auth_.invalidate();   // the token may have been revoked or expired early: get a new one and try once more
            continue;
        }
        return res;
    }
    error = describeStatus(401);
    return FleetHttpResponse();
}

FleetRead FleetApi::readCharge(const std::string& vin) {
    FleetRead out;
    std::string error;
    const FleetHttpResponse res = call("GET", "/api/1/vehicles/" + vin + "/vehicle_data?endpoints=charge_state", "", error);
    if (!res.sent) {
        out.error = error;
        return out;
    }
    if (res.status == 408) {
        out.asleep = true;
        out.error = describeStatus(408);
        return out;
    }
    if (res.status != 200) {
        out.error = describeStatus(res.status);
        return out;
    }
    if (!parseChargeState(res.body, out.charge)) {
        out.error = "Tesla's answer had no charge information.";
        return out;
    }
    out.ok = true;
    return out;
}

bool FleetApi::wake(const std::string& vin, std::string& error) {
    const FleetHttpResponse res = call("POST", "/api/1/vehicles/" + vin + "/wake_up", "", error);
    if (!res.sent) return false;
    if (res.status != 200) {
        error = describeStatus(res.status);
        return false;
    }
    return true;
}

bool FleetApi::signedCommand(const std::string& vin, const tesla::Bytes& routable, tesla::Bytes& reply, std::string& error) {
    const std::string body = "{\"routable_message\":\"" + base64Encode(routable) + "\"}";
    const FleetHttpResponse res = call("POST", "/api/1/vehicles/" + vin + "/signed_command", body, error);
    if (!res.sent) return false;
    if (res.status != 200) {
        error = describeStatus(res.status);
        return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, res.body) || !doc["response"].is<const char*>()) {
        error = "Tesla's answer to the command could not be read.";
        return false;
    }
    if (!base64Decode(doc["response"].as<std::string>(), reply)) {
        error = "Tesla's answer to the command was not valid.";
        return false;
    }
    return true;
}

}  // namespace fleet
}  // namespace solar
