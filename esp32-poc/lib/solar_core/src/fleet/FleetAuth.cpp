#include "fleet/FleetAuth.h"

#include <ArduinoJson.h>

#include "fleet/UrlEncode.h"

namespace solar {
namespace fleet {


void FleetAuth::reload() {
    creds_ = FleetCredentials();
    store_.load(creds_);
    access_.clear();
    expiresAt_ = 0;
    retryAt_ = 0;
    error_.clear();
    state_ = creds_.complete() ? State::Ok : State::NotConnected;
}

std::string FleetAuth::apiBase() const {
    return creds_.region == "eu" ? "https://fleet-api.prd.eu.vn.cloud.tesla.com" : "https://fleet-api.prd.na.vn.cloud.tesla.com";
}

void FleetAuth::connect(const FleetCredentials& creds) {
    store_.save(creds);
    reload();
}

void FleetAuth::forget() {
    store_.clear();
    reload();
}

std::string FleetAuth::accessToken() {
    if (!creds_.complete()) {
        state_ = State::NotConnected;
        error_ = "Not connected to a Tesla account yet.";
        return "";
    }
    const uint64_t now = nowSeconds();
    if (!access_.empty() && now + kRefreshMarginSeconds < expiresAt_) return access_;
    if (retryAt_ != 0 && now < retryAt_) return "";   // a recent attempt failed: wait before asking again
    return refresh() ? access_ : "";
}

bool FleetAuth::refresh() {
    FleetHttpRequest req;
    req.method = "POST";
    req.url = kTokenUrl;
    req.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
    req.body = "grant_type=refresh_token&client_id=" + urlEncode(creds_.clientId) + "&refresh_token=" + urlEncode(creds_.refreshToken);
    const FleetHttpResponse res = http_.send(req);
    const uint64_t now = nowSeconds();

    if (!res.sent) {
        state_ = State::Offline;
        error_ = "Could not reach Tesla (no internet?).";
        retryAt_ = now + kOfflineRetrySeconds;
        return false;
    }
    if (res.status == 200) {
        JsonDocument doc;
        if (!deserializeJson(doc, res.body) && doc["access_token"].is<const char*>()) {
            access_ = doc["access_token"].as<std::string>();
            const int lifetime = doc["expires_in"] | 3600;
            expiresAt_ = now + static_cast<uint64_t>(lifetime > 0 ? lifetime : 3600);
            if (doc["refresh_token"].is<const char*>()) {
                const std::string next = doc["refresh_token"].as<std::string>();
                if (!next.empty() && next != creds_.refreshToken) {
                    creds_.refreshToken = next;
                    store_.save(creds_);   // single use: keep the new one before anything else can go wrong
                }
            }
            state_ = State::Ok;
            error_.clear();
            retryAt_ = 0;
            return true;
        }
        state_ = State::Offline;
        error_ = "Tesla's answer could not be read.";
        retryAt_ = now + kOfflineRetrySeconds;
        return false;
    }
    if (res.status == 400 || res.status == 401 || res.status == 403) {
        state_ = State::NeedsSignIn;
        error_ = "Tesla no longer accepts the saved sign-in. Sign in again.";
        retryAt_ = now + kSignInRetrySeconds;
        return false;
    }
    state_ = State::Offline;
    error_ = res.status == 429 ? "Tesla says too many requests. Waiting." : "Tesla had a problem (code " + std::to_string(res.status) + ").";
    retryAt_ = now + kOfflineRetrySeconds * (res.status == 429 ? 4 : 1);
    return false;
}

}  // namespace fleet
}  // namespace solar
