#include "setup/SetupApi.h"

#include <ArduinoJson.h>

namespace solar::setup {

namespace {

HttpResponse text(int status, const std::string& body) {
    HttpResponse response;
    response.status = status;
    response.body = body;
    return response;
}

HttpResponse json(const JsonDocument& doc) {
    HttpResponse response;
    response.contentType = "application/json";
    serializeJson(doc, response.body);
    return response;
}

std::string field(const ApiHandler::Params& params, const char* key) {
    auto it = params.find(key);
    return it == params.end() ? std::string() : it->second;
}

}  // namespace

HttpResponse SetupApi::handle(const std::string& method, const std::string& path, const ApiHandler::Params& params,
                              bool hasSafeHeader) {
    if (!host_.active()) {
        return text(403, "Setup is off. Hold the BOOT button for 5 seconds to turn it on.");
    }
    if (method == "POST" && !hasSafeHeader) {
        return text(403, "Blocked: changing requests need the X-WattSync header (the setup page adds it).");
    }
    host_.userActive();

    if (path == "/api/setup/status" && method == "GET") return status();
    if (path == "/api/setup/networks" && method == "GET") return networks();
    if (path == "/api/setup/scan" && method == "POST") {
        host_.startScan();
        return text(200, "Scanning");
    }
    if (path == "/api/setup/save" && method == "POST") return save(params);
    return text(404, "Not found");
}

HttpResponse SetupApi::status() {
    const Credentials now = current();
    JsonDocument doc;
    doc["apName"] = kApName;
    doc["ssid"] = now.wifiSsid;
    doc["hasToken"] = !now.enphaseToken.empty();
    doc["hasUpdatePassword"] = !now.otaPassword.empty();
    doc["envoyIp"] = now.envoyIp;   // an address, not a secret
    doc["error"] = store_.loadTrial().lastError;
    doc["secondsLeft"] = host_.secondsLeft();
    return json(doc);
}

HttpResponse SetupApi::networks() {
    JsonDocument doc;
    doc["scanning"] = host_.scanning();
    doc["saved"] = current().wifiSsid;
    JsonArray list = doc["networks"].to<JsonArray>();
    for (const ScanEntry& entry : host_.networks()) {
        JsonObject item = list.add<JsonObject>();
        item["ssid"] = entry.ssid;
        item["rssi"] = entry.rssi;
        item["secure"] = entry.secure;
    }
    return json(doc);
}

HttpResponse SetupApi::save(const ApiHandler::Params& params) {
    Form form;
    form.ssid = field(params, "ssid");
    form.password = field(params, "password");
    form.open = field(params, "open") == "1";
    form.token = field(params, "token");
    form.envoyIp = field(params, "envoyIp");
    form.updatePassword = field(params, "updatePassword");

    const Credentials before = current();
    Validation checked = validate(form, before);
    if (!checked.ok()) return text(400, checked.error);

    const bool wifiChanged = checked.merged.wifiSsid != before.wifiSsid || checked.merged.wifiPassword != before.wifiPassword;
    store_.save(checked.merged);

    TrialState trial;   // clears any old error
    if (wifiChanged) {
        trial.pending = true;
        trial.previousSsid = before.wifiSsid;
        trial.previousPassword = before.wifiPassword;
    }
    store_.saveTrial(trial);
    host_.restartSoon();

    JsonDocument doc;
    doc["ok"] = true;
    doc["wifiChanged"] = wifiChanged;
    doc["ssid"] = checked.merged.wifiSsid;
    return json(doc);
}

}  // namespace solar::setup
