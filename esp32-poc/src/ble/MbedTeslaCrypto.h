#pragma once

#include "tesla/TeslaCrypto.h"

namespace device {

/** The Tesla protocol's cryptography on the ESP32's mbedTLS (hardware-accelerated where the chip can). Checked on the board with `tesla selftest`. */
class MbedTeslaCrypto : public solar::tesla::Crypto {
public:
    solar::tesla::Bytes sha256(const solar::tesla::Bytes& data) override;
    solar::tesla::Bytes hmacSha256(const solar::tesla::Bytes& key, const solar::tesla::Bytes& data) override;
    bool aesGcmEncrypt(const solar::tesla::Bytes& key, const solar::tesla::Bytes& nonce, const solar::tesla::Bytes& aad, const solar::tesla::Bytes& plain,
                       solar::tesla::Bytes& cipher, solar::tesla::Bytes& tag) override;
    bool aesGcmDecrypt(const solar::tesla::Bytes& key, const solar::tesla::Bytes& nonce, const solar::tesla::Bytes& aad, const solar::tesla::Bytes& cipher,
                       const solar::tesla::Bytes& tag, solar::tesla::Bytes& plain) override;
    bool ecdh(const solar::tesla::Bytes& privateKey, const solar::tesla::Bytes& peerPublic65, solar::tesla::Bytes& secretX) override;
    bool publicKeyFor(const solar::tesla::Bytes& privateKey, solar::tesla::Bytes& public65) override;
    bool generateKeyPair(solar::tesla::Bytes& privateKey, solar::tesla::Bytes& public65) override;
    void random(uint8_t* out, size_t n) override;
};

}  // namespace device
