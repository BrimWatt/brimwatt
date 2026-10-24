#include "enphase/EnphaseClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "common/Log.h"
#include "config/DeviceConfig.h"
#include "net/Mdns.h"

namespace device {

bool EnphaseClient::findGateway(std::string& error) {
    if (haveIp_) return true;
    if (strlen(fixedIp_) > 0) {
        if (!ip_.fromString(fixedIp_)) {
            error = "ENVOY_IP in secrets.h is not a valid address";
            return false;
        }
        haveIp_ = true;
        return true;
    }
    uint32_t start = millis();
    if (!Mdns::resolve(kEnvoyHost, ip_, 3000)) {
        error = "envoy.local not found (set ENVOY_IP in secrets.h)";
        return false;
    }
    LOG_INFO("Envoy: envoy.local is %s (%lu ms)", ip_.toString().c_str(), (unsigned long)(millis() - start));
    haveIp_ = true;
    return true;
}

bool EnphaseClient::read(solar::MeterReading& out, std::string& error) {
    if (!findGateway(error)) return false;

    WiFiClientSecure client;
    client.setInsecure();   // the Envoy's certificate is self-signed, as in the Java app

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    if (!http.begin(client, ip_.toString(), 443, "/ivp/meters/readings", true)) {
        error = "could not start the HTTPS request";
        return false;
    }
    http.addHeader("Authorization", String("Bearer ") + token_);
    http.addHeader("Accept", "application/json");

    uint32_t start = millis();
    int code = http.GET();
    uint32_t took = millis() - start;

    if (code <= 0) {
        error = std::string("request failed: ") + HTTPClient::errorToString(code).c_str();
        haveIp_ = false;   // look the gateway up again next time, in case its address changed
        http.end();
        return false;
    }
    if (code == 401 || code == 403) {
        error = "HTTP " + std::to_string(code) + ": the access token was rejected; renew ENPHASE_TOKEN";
        http.end();
        return false;
    }
    if (code != 200) {
        error = "HTTP " + std::to_string(code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError parse = deserializeJson(doc, body);
    if (parse) {
        error = std::string("bad JSON: ") + parse.c_str();
        return false;
    }
    JsonArray meters = doc.as<JsonArray>();
    if (meters.size() < 2) {
        error = "expected 2 meter readings, got " + std::to_string(meters.size());
        return false;
    }
    out.solarWatts = max(0.0, meters[0]["activePower"].as<double>());
    out.gridWatts = meters[1]["activePower"].as<double>();
    LOG_DEBUG("Envoy: HTTP 200 in %lu ms, %u bytes", (unsigned long)took, (unsigned)body.length());
    return true;
}

}  // namespace device
