#pragma once

#include <string>

#include "common/Sha256.h"

namespace solar {

/**
 * A short one-way fingerprint of the ID tag a charger sends when a charge starts (a card, the charger's own app, or the random tag
 * sent on a remote start). Only this fingerprint is ever stored or shown, never the tag itself: the same tag always gives the same
 * fingerprint, so charges can be told apart, but the tag cannot be read back. Same result as the Java app's IdTagHash.
 * The first 8 hex digits of SHA-256("wattsync:" + tag), or "" if the charger sent no tag.
 */
inline std::string idTagHash(const std::string& idTag) {
    bool blank = true;
    for (char c : idTag) if (c != ' ' && c != '\t' && c != '\n' && c != '\r') { blank = false; break; }
    if (blank) return "";
    const auto digest = Sha256::hash("wattsync:" + idTag);
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 4; i++) { out += hex[digest[i] >> 4]; out += hex[digest[i] & 15]; }
    return out;
}

}  // namespace solar
