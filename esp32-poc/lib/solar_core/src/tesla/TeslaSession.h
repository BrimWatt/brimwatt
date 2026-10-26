#pragma once

#include <string>

#include "tesla/TeslaCrypto.h"
#include "tesla/TeslaMessages.h"

namespace solar {
namespace tesla {

/** The signed-data prefix: tag, length, value for each item in tag order, closed by 0xFF. Hashed (or MAC'd) together with what it protects. */
class Metadata {
public:
    void add(int tag, const Bytes& value) {
        out_.push_back(static_cast<uint8_t>(tag));
        out_.push_back(static_cast<uint8_t>(value.size()));
        out_.insert(out_.end(), value.begin(), value.end());
    }
    void addByte(int tag, int value) { add(tag, Bytes(1, static_cast<uint8_t>(value))); }
    void addU32(int tag, uint32_t v) {
        add(tag, Bytes{static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)});
    }
    /** The bytes up to and including the closing 0xFF. */
    Bytes finish() const {
        Bytes b = out_;
        b.push_back(static_cast<uint8_t>(kTagEnd));
        return b;
    }

private:
    Bytes out_;
};

/**
 * One secure session with one of the car's two computers (a "domain"): the handshake that turns our private key and the car's public
 * key into a shared AES key, then signed and encrypted commands. Pure logic; the Bluetooth link lives elsewhere.
 */
class Session {
public:
    Session(Crypto& crypto, const Bytes& privateKey, const Bytes& publicKey65, const std::string& vin, int domain)
        : c_(crypto), priv_(privateKey), pub_(publicKey65), vin_(vin), domain_(domain) {}

    /** The first message: our public key and a fresh challenge. The caller adds routing and sends it. */
    Routable handshakeRequest();

    /** Checks the car's answer to the handshake (its HMAC must match) and, if good, starts the session. `nowMs` is the local clock. */
    bool acceptSessionInfo(const Routable& reply, uint32_t nowMs, std::string& error);

    bool ready() const { return ready_; }
    /** The car says our key is not on its list (nothing to do but pair again). */
    bool keyUnknown() const { return keyUnknown_; }
    const SessionInfo& info() const { return info_; }

    /** A signed, encrypted command. `requestTag` receives the GCM tag (the reply is tied to it). */
    Routable command(const Bytes& payload, uint32_t flags, uint32_t ttlSeconds, uint32_t nowMs, Bytes& requestTag);

    /** Decrypts the car's reply to the command whose tag is `requestTag`. */
    bool openReply(const Routable& reply, const Bytes& requestTag, Bytes& plain, std::string& error);

    // exposed for the known-answer tests
    static Bytes sharedKey(Crypto& c, const Bytes& secretX);
    Bytes sessionInfoMac(const Bytes& encodedInfo, const Bytes& challenge) const;
    Bytes commandMetadata(const Bytes& epoch, uint32_t expiresAt, uint32_t counter, uint32_t flags) const;
    void setKeyForTest(const Bytes& key) { key_ = key; }
    void setChallengeForTest(const Bytes& challenge) { challenge_ = challenge; }

private:
    Crypto& c_;
    Bytes priv_, pub_;
    std::string vin_;
    int domain_;
    Bytes challenge_, key_, sessionInfoKey_;
    SessionInfo info_;
    uint32_t counter_ = 0;
    uint32_t infoAtMs_ = 0;
    uint32_t lastReplyCounter_ = 0;
    bool ready_ = false;
    bool keyUnknown_ = false;
};

}  // namespace tesla
}  // namespace solar
