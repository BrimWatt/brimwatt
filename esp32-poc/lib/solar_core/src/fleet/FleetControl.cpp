#include "fleet/FleetControl.h"

#include <ArduinoJson.h>

#include <cctype>
#include <cstdlib>

#include "fleet/Base64.h"
#include "fleet/UrlEncode.h"
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

bool FleetManager::isLocalOrigin(const std::string& origin) {
    const std::string scheme = "http://";
    if (origin.size() <= scheme.size() || origin.compare(0, scheme.size(), scheme) != 0) return false;
    std::string host = origin.substr(scheme.size());
    const size_t colon = host.find(':');
    if (colon != std::string::npos) {
        const std::string port = host.substr(colon + 1);
        if (port.empty() || port.size() > 5) return false;
        for (char c : port) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        host = host.substr(0, colon);
    }
    if (host.empty() || host.size() > 60) return false;
    for (char& c : host) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') return false;   // no paths, no user info, no odd characters
    }
    if (host == "localhost") return true;
    if (host.size() > 6 && host.compare(host.size() - 6, 6, ".local") == 0) return true;
    // dotted IPv4 in a private range
    int parts[4];
    int count = 0;
    size_t at = 0;
    bool ended = false;   // the fourth number must be the end of the address: "192.168.1.1.5" has five
    while (count < 4) {
        const size_t dot = host.find('.', at);
        const std::string piece = host.substr(at, dot == std::string::npos ? std::string::npos : dot - at);
        if (piece.empty() || piece.size() > 3) return false;
        for (char c : piece) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        const int v = std::atoi(piece.c_str());
        if (v > 255) return false;
        parts[count++] = v;
        if (dot == std::string::npos) { ended = true; break; }
        at = dot + 1;
    }
    if (count != 4 || !ended) return false;
    return parts[0] == 10 || parts[0] == 127 || (parts[0] == 192 && parts[1] == 168) || (parts[0] == 172 && parts[1] >= 16 && parts[1] <= 31) ||
           (parts[0] == 169 && parts[1] == 254);
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
    FleetApp app;
    apps_.load(app);
    s.appReady = app.ready();
    s.domain = app.domain;
    std::lock_guard<std::mutex> lock(m_);
    s.signIn = signIn_;
    s.signInText = signInText_;
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

std::string FleetManager::saveApp(const FleetApp& app, const std::string& keyHex) {
    if (app.clientId.empty() || app.clientId.size() > 100) return "The client id is missing or too long.";
    for (char c : app.clientId)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return "The client id has characters that do not belong in one.";
    if (app.clientSecret.size() < 8 || app.clientSecret.size() > 300) return "The client secret looks wrong (too short or too long).";
    for (char c : app.clientSecret)
        if (std::isspace(static_cast<unsigned char>(c))) return "The client secret must not contain spaces or line breaks.";
    if (app.region != "na" && app.region != "eu") return "region must be na or eu.";
    // A bare host name: no scheme, no path, at least one dot.
    if (app.domain.size() < 4 || app.domain.size() > 100 || app.domain.find('.') == std::string::npos) return "The domain must be a host name like wattsync.netlify.app.";
    for (char c : app.domain)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') return "The domain must be only a host name (no https://, no path).";
    tesla::Bytes key;
    if (!keyHex.empty()) {
        tesla::Bytes pub;
        if (!parseKeyHex(keyHex, key) || !crypto_.publicKeyFor(key, pub)) return "The signing key must be 64 hex digits and a valid P-256 key.";
    }
    apps_.save(app);
    if (!key.empty()) identity_.saveKey(key);
    return "";
}

std::string FleetManager::beginSignIn(const std::string& origin, std::string& url) {
    FleetApp app;
    if (!apps_.load(app) || !app.ready()) return "The Tesla app is not set up on this board yet. Run the setup tool on your computer first.";
    if (!isLocalOrigin(origin)) return "The sign-in can only come back to this board's own address on your home network.";
    std::string nonce;
    for (int i = 0; i < 4; i++) {
        static const char* hex = "0123456789abcdef";
        const uint32_t r = platform_.random32();
        for (int b = 28; b >= 0; b -= 4) nonce += hex[(r >> b) & 15];
    }
    tesla::Bytes raw(origin.begin(), origin.end());
    std::string tail = base64Encode(raw);
    for (char& c : tail) { if (c == '+') c = '-'; else if (c == '/') c = '_'; }
    while (!tail.empty() && tail.back() == '=') tail.pop_back();
    const std::string state = nonce + "." + tail;
    {
        std::lock_guard<std::mutex> lock(m_);
        nonce_ = nonce;
        origin_ = origin;
        expiresAtMs_ = platform_.nowMillis() + static_cast<uint64_t>(kSignInWindowSeconds) * 1000;
        signIn_ = "waiting";
        signInText_ = "Waiting for you to sign in to Tesla…";
    }
    url = std::string(kAuthorizeUrl) + "?client_id=" + urlEncode(app.clientId) +
          "&redirect_uri=" + urlEncode("https://" + app.domain + kCallbackPage) + "&response_type=code&scope=" + urlEncode(kScopes) +
          "&state=" + urlEncode(state) + "&locale=en-US&prompt_missing_scopes=true";
    return "";
}

void FleetManager::finish(bool ok, const std::string& text) {
    std::lock_guard<std::mutex> lock(m_);
    signIn_ = ok ? "done" : "failed";
    signInText_ = text;
}

std::string FleetManager::completeSignIn(const std::string& code, const std::string& state) {
    FleetApp app;
    if (!apps_.load(app) || !app.ready()) return "The Tesla app is not set up on this board.";
    if (code.empty() || code.size() > 2000) return "Tesla sent no sign-in code.";
    for (char c : code)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.' && c != '~') return "Tesla's sign-in code has characters that do not belong in one.";
    {
        std::lock_guard<std::mutex> lock(m_);
        const std::string sent = state.substr(0, state.find('.'));
        const bool fresh = !nonce_.empty() && platform_.nowMillis() < expiresAtMs_;
        if (!fresh || sent != nonce_) {
            signIn_ = "failed";
            signInText_ = fresh ? "That sign-in did not start on this board." : "That sign-in has expired. Start it again.";
            return signInText_;
        }
        nonce_.clear();   // one use only
        signIn_ = "working";
        signInText_ = "Signing in…";
    }
    if (!executor_.start([this, app, code]() { exchange(app, code); })) {
        finish(false, "Not enough memory just now. Try again in a moment.");
        return "Not enough memory just now. Try again in a moment.";
    }
    return "";
}

void FleetManager::exchange(const FleetApp& app, const std::string& code) {
    FleetHttpRequest req;
    req.method = "POST";
    req.url = FleetAuth::kTokenUrl;
    req.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
    const std::string audience = app.region == "eu" ? "https://fleet-api.prd.eu.vn.cloud.tesla.com" : "https://fleet-api.prd.na.vn.cloud.tesla.com";
    req.body = "grant_type=authorization_code&client_id=" + urlEncode(app.clientId) + "&client_secret=" + urlEncode(app.clientSecret) +
               "&code=" + urlEncode(code) + "&audience=" + urlEncode(audience) + "&redirect_uri=" + urlEncode("https://" + app.domain + kCallbackPage);
    const FleetHttpResponse res = http_.send(req);
    if (!res.sent) { finish(false, "Could not reach Tesla (no internet?)."); return; }
    if (res.status != 200) {
        finish(false, res.status == 400 || res.status == 401 ? "Tesla did not accept the sign-in (the code may have expired, or the app's secret or redirect address is wrong). Try again."
                                                              : "Tesla answered with code " + std::to_string(res.status) + ".");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, res.body) || !doc["refresh_token"].is<const char*>()) {
        finish(false, "Tesla's answer had no sign-in for the board to keep (is offline_access one of the app's scopes?).");
        return;
    }
    FleetCredentials creds;
    creds.clientId = app.clientId;
    creds.refreshToken = doc["refresh_token"].as<std::string>();
    creds.region = app.region;
    auth_.connect(creds);   // saved at once: the refresh token is single use

    std::vector<std::string> vins;
    std::string error;
    if (!api_.listVehicles(vins, error)) {
        finish(true, "Signed in, but the list of cars could not be read: " + error + " Enter the VIN below.");
        return;
    }
    if (vins.size() == 1) {
        identity_.saveVin(vins[0]);
        finish(true, "Signed in. Car found: " + tesla::maskVin(vins[0]) + ".");
    } else if (vins.empty()) {
        finish(true, "Signed in, but this Tesla account has no car. Enter a VIN below if the car is shared with it.");
    } else {
        finish(true, "Signed in. This account has " + std::to_string(vins.size()) + " cars: enter the VIN of the one to control.");
    }
}

std::string FleetManager::setVin(const std::string& vin) {
    const std::string cleaned = cleanVin(vin);
    if (!tesla::isValidVin(cleaned)) return "That is not a VIN (17 letters and digits, no I, O or Q).";
    identity_.saveVin(cleaned);
    return "";
}

}  // namespace fleet
}  // namespace solar
