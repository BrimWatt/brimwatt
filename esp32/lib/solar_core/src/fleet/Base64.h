#pragma once

#include <string>

#include "tesla/Proto.h"

namespace solar {
namespace fleet {

/** Standard base64 with padding (what the Fleet API uses for signed messages). */
inline std::string base64Encode(const tesla::Bytes& in) {
    static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    for (size_t i = 0; i < in.size(); i += 3) {
        uint32_t v = static_cast<uint32_t>(in[i]) << 16;
        if (i + 1 < in.size()) v |= static_cast<uint32_t>(in[i + 1]) << 8;
        if (i + 2 < in.size()) v |= in[i + 2];
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += i + 1 < in.size() ? kAlphabet[(v >> 6) & 63] : '=';
        out += i + 2 < in.size() ? kAlphabet[v & 63] : '=';
    }
    return out;
}

/** False for anything that is not valid base64 (padding is optional, whitespace is ignored). */
inline bool base64Decode(const std::string& in, tesla::Bytes& out) {
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;   // '-' and '_' are the URL-safe forms
        else if (c == '/' || c == '_') v = 63;
        else if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        else return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return true;
}

}  // namespace fleet
}  // namespace solar
