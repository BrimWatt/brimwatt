#include "tesla/TeslaBle.h"

#include <cctype>
#include <cstdio>

namespace solar {
namespace tesla {

bool isValidVin(const std::string& vin) {
    if (vin.size() != 17) return false;
    for (char c : vin) {
        if (!std::isalnum(static_cast<unsigned char>(c))) return false;
        char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (u == 'I' || u == 'O' || u == 'Q') return false;
    }
    return true;
}

static uint32_t rotl(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

std::vector<uint8_t> sha1(const std::string& data) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    std::vector<uint8_t> msg(data.begin(), data.end());
    uint64_t bits = static_cast<uint64_t>(msg.size()) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>(bits >> (i * 8)));

    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(msg[off + i * 4]) << 24) | (uint32_t(msg[off + i * 4 + 1]) << 16) |
                   (uint32_t(msg[off + i * 4 + 2]) << 8) | uint32_t(msg[off + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = t;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    std::vector<uint8_t> out;
    for (uint32_t h : {h0, h1, h2, h3, h4})
        for (int i = 3; i >= 0; --i) out.push_back(static_cast<uint8_t>(h >> (i * 8)));
    return out;
}

std::string advertisedName(const std::string& vin) {
    if (!isValidVin(vin)) return "";
    std::string upper;
    for (char c : vin) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::vector<uint8_t> digest = sha1(upper);
    std::string name = "S";
    char hex[3];
    for (int i = 0; i < 8; ++i) {
        std::snprintf(hex, sizeof hex, "%02x", digest[i]);
        name += hex;
    }
    return name + "C";
}

std::string maskVin(const std::string& vin) {
    if (vin.size() != 17) return "(invalid)";
    return vin.substr(0, 3) + std::string(10, '*') + vin.substr(13);
}

void ScanStats::start(uint32_t nowMs, uint32_t durationMs) {
    startMs_ = nowMs;
    durationMs_ = durationMs;
    sightings_ = 0;
    min_ = max_ = 0;
    sum_ = 0;
    windows_.assign((durationMs + kWindowMillis - 1) / kWindowMillis, false);
}

void ScanStats::heard(uint32_t nowMs, int rssi) {
    if (nowMs < startMs_) return;
    uint32_t idx = (nowMs - startMs_) / kWindowMillis;
    if (idx >= windows_.size()) return;
    windows_[idx] = true;
    if (sightings_ == 0) min_ = max_ = rssi;
    if (rssi < min_) min_ = rssi;
    if (rssi > max_) max_ = rssi;
    sum_ += rssi;
    ++sightings_;
}

int ScanStats::heardPercent() const {
    if (windows_.empty()) return 0;
    int hit = 0;
    for (bool w : windows_) hit += w ? 1 : 0;
    return hit * 100 / static_cast<int>(windows_.size());
}

const char* ScanStats::verdict() const {
    if (sightings_ == 0) return "Not heard";
    int pct = heardPercent();
    int avg = average();
    if (pct >= 90 && avg >= -75) return "Good";
    if (pct >= 70 && avg >= -85) return "OK";
    return "Weak";
}

std::string ScanStats::summary() const {
    char buf[200];
    if (sightings_ == 0) {
        std::snprintf(buf, sizeof buf, "Not heard in %u s. Check the VIN, that the car is within range (try the parking spot) and that nothing else holds its Bluetooth.",
                      static_cast<unsigned>(durationMs_ / 1000));
    } else {
        std::snprintf(buf, sizeof buf, "%s: heard %d times, in %d%% of the 5 s windows, signal %d dBm average (%d weakest, %d strongest).", verdict(), sightings_,
                      heardPercent(), average(), weakest(), strongest());
    }
    return buf;
}

}  // namespace tesla
}  // namespace solar
