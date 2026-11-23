#include "setup/SetupLogic.h"

#include <algorithm>
#include <cctype>

namespace solar::setup {

namespace {

std::string trim(const std::string& text) {
    const char* spaces = " \t\r\n";
    size_t begin = text.find_first_not_of(spaces);
    if (begin == std::string::npos) return "";
    size_t end = text.find_last_not_of(spaces);
    return text.substr(begin, end - begin + 1);
}

bool blank(const std::string& text) { return trim(text).empty(); }

bool hasControlCharacter(const std::string& text) {
    for (unsigned char c : text) {
        if (c < 0x20 || c == 0x7f) return true;
    }
    return false;
}

bool hasWhitespace(const std::string& text) {
    for (unsigned char c : text) {
        if (std::isspace(c)) return true;
    }
    return false;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool tokenCharacters(const std::string& text) {
    for (unsigned char c : text) {
        if (!std::isalnum(c) && std::string("-_.~+/=").find(static_cast<char>(c)) == std::string::npos) return false;
    }
    return true;
}

std::string stripLineBreaks(const std::string& text) {
    size_t begin = text.find_first_not_of("\r\n");
    if (begin == std::string::npos) return "";
    size_t end = text.find_last_not_of("\r\n");
    return text.substr(begin, end - begin + 1);
}

}  // namespace

Credentials effective(const Credentials& saved, const Credentials& compiledDefaults) {
    Credentials result;
    result.wifiSsid = saved.wifiSsid.empty() ? compiledDefaults.wifiSsid : saved.wifiSsid;
    // A saved network keeps its own password, even an empty one (an open network); the default's goes with the default's name.
    result.wifiPassword = saved.wifiSsid.empty() ? compiledDefaults.wifiPassword : saved.wifiPassword;
    result.enphaseToken = saved.enphaseToken.empty() ? compiledDefaults.enphaseToken : saved.enphaseToken;
    result.otaPassword = saved.otaPassword.empty() ? compiledDefaults.otaPassword : saved.otaPassword;
    if (saved.envoyIp == "auto") {
        result.envoyIp.clear();
    } else {
        result.envoyIp = saved.envoyIp.empty() ? compiledDefaults.envoyIp : saved.envoyIp;
    }
    return result;
}

namespace {
/** One pair of matching quotes around the whole text, with what is inside trimmed. */
std::string withoutQuotes(const std::string& text) {
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') || (text.front() == '\'' && text.back() == '\''))) {
        return trim(text.substr(1, text.size() - 2));
    }
    return text;
}
}  // namespace

std::string cleanToken(const std::string& pasted) {
    // Quotes can sit outside or inside "Bearer ", so look for them before and after it.
    std::string token = withoutQuotes(trim(pasted));
    if (lower(token).rfind("bearer ", 0) == 0) token = withoutQuotes(trim(token.substr(7)));
    return token;
}

bool isValidIpv4(const std::string& text) {
    int parts = 0;
    size_t i = 0;
    int nonZero = 0;
    while (i <= text.size()) {
        size_t dot = text.find('.', i);
        std::string part = text.substr(i, dot == std::string::npos ? std::string::npos : dot - i);
        if (part.empty() || part.size() > 3) return false;
        for (unsigned char c : part) {
            if (!std::isdigit(c)) return false;
        }
        if (part.size() > 1 && part[0] == '0') return false;   // "010" would be read as octal by some tools
        int value = std::stoi(part);
        if (value > 255) return false;
        if (value != 0) nonZero++;
        parts++;
        if (dot == std::string::npos) break;
        i = dot + 1;
    }
    return parts == 4 && nonZero > 0;
}

Validation validate(const Form& form, const Credentials& current) {
    Validation result;
    result.merged = current;

    // --- Wi-Fi network ---
    const std::string ssid = stripLineBreaks(form.ssid);
    if (ssid.empty()) {
        if (current.wifiSsid.empty()) {
            result.error = "Choose your Wi-Fi network.";
            return result;
        }
    } else {
        if (ssid.size() > kMaxSsidBytes || hasControlCharacter(ssid)) {
            result.error = "The Wi-Fi name must be 1 to 32 characters.";
            return result;
        }
        const bool changed = ssid != current.wifiSsid;
        if (form.open) {
            result.merged.wifiPassword.clear();
        } else if (form.password.empty()) {
            if (changed || current.wifiPassword.empty()) {
                result.error = "Type the Wi-Fi password.";
                return result;
            }   // same network, nothing typed: keep the saved password
        } else {
            if (form.password.size() < kMinWifiPassword || form.password.size() > kMaxWifiPassword || hasControlCharacter(form.password)) {
                result.error = "The Wi-Fi password must be 8 to 63 characters.";
                return result;
            }
            result.merged.wifiPassword = form.password;
        }
        result.merged.wifiSsid = ssid;
    }

    // --- Enphase token ---
    if (blank(form.token)) {
        if (current.enphaseToken.empty()) {
            result.error = "Paste the Enphase token.";
            return result;
        }
    } else {
        const std::string token = cleanToken(form.token);
        if (token.size() < kMinToken || token.size() > kMaxToken || !tokenCharacters(token)) {
            result.error = "That does not look like an Enphase token. Paste the whole token, with nothing around it.";
            return result;
        }
        result.merged.enphaseToken = token;
    }

    // --- Envoy address ---
    const std::string ip = trim(form.envoyIp);
    if (ip.empty()) {
        result.merged.envoyIp = "auto";
    } else if (!isValidIpv4(ip)) {
        result.error = "The Envoy address must look like 192.168.1.50, or be left empty to find it automatically.";
        return result;
    } else {
        result.merged.envoyIp = ip;
    }

    // --- Update password ---
    if (form.updatePassword.empty()) {
        if (current.otaPassword.empty()) {
            result.error = "Choose an update password of 8 to 64 characters.";
            return result;
        }
    } else {
        if (form.updatePassword.size() < kMinUpdatePassword || form.updatePassword.size() > kMaxUpdatePassword ||
            hasControlCharacter(form.updatePassword) || hasWhitespace(form.updatePassword)) {
            result.error = "The update password must be 8 to 64 characters, without spaces.";
            return result;
        }
        result.merged.otaPassword = form.updatePassword;
    }
    return result;
}

std::vector<ScanEntry> tidyScan(std::vector<ScanEntry> raw) {
    std::vector<ScanEntry> tidy;
    for (auto& entry : raw) {
        if (entry.ssid.empty() || hasControlCharacter(entry.ssid)) continue;   // hidden networks cannot be listed
        auto same = std::find_if(tidy.begin(), tidy.end(), [&](const ScanEntry& other) { return other.ssid == entry.ssid; });
        if (same == tidy.end()) {
            tidy.push_back(std::move(entry));
        } else if (entry.rssi > same->rssi) {
            *same = std::move(entry);   // a mesh or repeater shows the name several times: keep the strongest
        }
    }
    std::sort(tidy.begin(), tidy.end(), [](const ScanEntry& a, const ScanEntry& b) {
        return a.rssi != b.rssi ? a.rssi > b.rssi : a.ssid < b.ssid;
    });
    return tidy;
}

std::string joinFailureMessage(const std::string& ssid, const std::string& reason, bool previousKept) {
    std::string message = "Could not join \"" + ssid + "\": " + reason + ".";
    message += previousKept ? " The previous Wi-Fi was put back." : " No Wi-Fi is saved.";
    return message;
}

TrialOutcome finishTrial(CredentialStore& store, bool joined, const std::string& reason) {
    TrialOutcome outcome;
    TrialState trial = store.loadTrial();
    if (!trial.pending) return outcome;
    outcome.wasTrial = true;
    outcome.joined = joined;

    if (joined) {
        store.saveTrial(TrialState{});   // the new network works: nothing to undo, nothing to report
        return outcome;
    }
    Credentials saved = store.load();
    const std::string tried = saved.wifiSsid;
    saved.wifiSsid = trial.previousSsid;
    saved.wifiPassword = trial.previousPassword;
    store.save(saved);

    TrialState after;
    after.lastError = joinFailureMessage(tried, reason, !trial.previousSsid.empty());
    store.saveTrial(after);
    return outcome;
}

}  // namespace solar::setup
