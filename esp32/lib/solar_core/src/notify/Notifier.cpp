#include "notify/Notifier.h"

#include <cctype>
#include <cstring>

namespace solar {

namespace {
const char* const kWorthSending[] = {"Charging ended", "Lost contact with the solar meter", "Solar meter is back",
                                      "Charger disconnected", "The saved settings were damaged",
                                      "Departure goal needs charging now", "Car not reachable", "Car reachable again", "Control moved"};

bool startsWith(const std::string& text, const char* prefix) { return text.compare(0, std::strlen(prefix), prefix) == 0; }

/** The part of an address after the scheme, up to the first slash; empty if there is no http(s) scheme. */
std::string hostPart(const std::string& url, std::string& scheme) {
    size_t start;
    if (startsWith(url, "https://")) { scheme = "https"; start = 8; }
    else if (startsWith(url, "http://")) { scheme = "http"; start = 7; }
    else return "";
    size_t end = url.find_first_of("/?#", start);
    return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
}
}  // namespace

bool Notifier::isValidUrl(const std::string& value) {
    if (value.empty()) return true;
    if (value.size() > kMaxUrlLength) return false;
    for (unsigned char c : value) {
        if (c <= ' ' || c == '"' || c == '\\' || c >= 0x7f) return false;   // spaces, quotes, control and non-ASCII characters
    }
    std::string scheme;
    return !hostPart(value, scheme).empty();
}

bool Notifier::isWorthSending(const std::string& eventText) {
    for (const char* prefix : kWorthSending) if (startsWith(eventText, prefix)) return true;
    return false;
}

std::string Notifier::maskedHost(const std::string& url) {
    std::string scheme;
    std::string host = hostPart(url, scheme);
    return host.empty() ? std::string() : scheme + "://" + host + "/…";
}

std::string Notifier::escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (static_cast<unsigned char>(c) < 0x20) out += ' ';
        else out += c;
    }
    return out;
}

std::string Notifier::body(const std::string& text) {
    const std::string t = escape(text);
    return "{\"text\":\"" + t + "\",\"content\":\"" + t + "\"}";
}

void Notifier::onEvent(const std::string& text) {
    const std::string& url = state_.notifyUrl();
    if (url.empty() || !isWorthSending(text)) return;
    const uint64_t now = platform_.nowMillis();
    auto it = lastSent_.find(text);
    if (it != lastSent_.end() && now - it->second < kRepeatMillis) return;
    if (lastSent_.size() > 20) lastSent_.clear();
    lastSent_[text] = now;
    sender_.sendAsync(url, body("Brimwatt: " + text));
}

std::string Notifier::sendTest() {
    if (state_.notifyUrl().empty()) return "No address set";
    return sender_.sendNow(state_.notifyUrl(), body("Brimwatt: test message, notifications work"));
}

}  // namespace solar
