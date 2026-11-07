#include "fleet/FleetControl.h"

#include <cctype>

#include "tesla/TeslaBle.h"

namespace solar {
namespace fleet {

std::string FleetManager::cleanVin(const std::string& raw) {
    std::string vin;
    for (char c : raw) {
        if (c == ' ' || c == '-' || c == '\t') continue;
        vin += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return vin;
}

bool FleetManager::parseKeyHex(const std::string& hex, tesla::Bytes& out) {
    out.clear();
    if (hex.size() != 64) return false;
    auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (size_t i = 0; i < 64; i += 2) {
        const int hi = digit(hex[i]), lo = digit(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<uint8_t>(hi * 16 + lo));
    }
    return true;
}

FleetStatus FleetManager::status() const {
    FleetStatus s;
    s.supported = true;
    s.connected = auth_.connected();
    switch (auth_.state()) {
        case FleetAuth::State::NotConnected: s.state = "not_connected"; break;
        case FleetAuth::State::Ok: s.state = "ok"; break;
        case FleetAuth::State::NeedsSignIn: s.state = "needs_sign_in"; break;
        case FleetAuth::State::Offline: s.state = "offline"; break;
    }
    s.error = auth_.error();
    s.region = auth_.region();
    const std::string id = auth_.clientId();
    if (id.size() >= 8) s.client = id.substr(0, 4) + "..." + id.substr(id.size() - 2);
    else if (!id.empty()) s.client = "set";
    s.hasVin = !identity_.vin().empty();
    tesla::Bytes key;
    s.hasKey = identity_.privateKey(key);
    return s;
}

std::string FleetManager::connect(const FleetCredentials& creds, const std::string& vin, const std::string& keyHex) {
    if (creds.clientId.empty() || creds.clientId.size() > 100) return "The client id is missing or too long.";
    for (char c : creds.clientId)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return "The client id has characters that do not belong in one.";
    if (creds.refreshToken.size() < 20 || creds.refreshToken.size() > 4000) return "The refresh token looks wrong (too short or too long).";
    for (char c : creds.refreshToken)
        if (std::isspace(static_cast<unsigned char>(c))) return "The refresh token must not contain spaces or line breaks.";
    if (creds.region != "na" && creds.region != "eu") return "region must be na or eu.";

    std::string cleanedVin = cleanVin(vin);
    if (!cleanedVin.empty() && !tesla::isValidVin(cleanedVin)) return "That is not a VIN (17 letters and digits, no I, O or Q).";
    tesla::Bytes key;
    if (!keyHex.empty()) {
        tesla::Bytes pub;
        if (!parseKeyHex(keyHex, key) || !crypto_.publicKeyFor(key, pub)) return "The signing key must be 64 hex digits and a valid P-256 key.";
    }
    // Everything checked: now save (the account first, then the optional extras).
    auth_.connect(creds);
    if (!cleanedVin.empty()) identity_.saveVin(cleanedVin);
    if (!key.empty()) identity_.saveKey(key);
    return "";
}

}  // namespace fleet
}  // namespace solar
