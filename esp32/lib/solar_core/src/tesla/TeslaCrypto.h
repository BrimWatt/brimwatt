#pragma once

#include "tesla/Proto.h"

namespace solar {
namespace tesla {

/**
 * The few cryptographic operations the Tesla protocol needs. The firmware implements this with mbedTLS (src/ble/MbedTeslaCrypto),
 * the host tests with OpenSSL (test/support/OpenSslCrypto.h). Both are checked against the known-answer values Tesla publishes
 * (TeslaVectors.h), on the computer and, with the `tesla selftest` console command, on the board.
 */
class Crypto {
public:
    virtual ~Crypto() {}
    virtual Bytes sha256(const Bytes& data) = 0;
    virtual Bytes hmacSha256(const Bytes& key, const Bytes& data) = 0;
    /** AES-128-GCM with a 12-byte nonce and a 16-byte tag. */
    virtual bool aesGcmEncrypt(const Bytes& key, const Bytes& nonce, const Bytes& aad, const Bytes& plain, Bytes& cipher, Bytes& tag) = 0;
    virtual bool aesGcmDecrypt(const Bytes& key, const Bytes& nonce, const Bytes& aad, const Bytes& cipher, const Bytes& tag, Bytes& plain) = 0;
    /** P-256: the shared secret's x coordinate (32 bytes) from our private key and the other side's 65-byte public key. */
    virtual bool ecdh(const Bytes& privateKey, const Bytes& peerPublic65, Bytes& secretX) = 0;
    virtual bool publicKeyFor(const Bytes& privateKey, Bytes& public65) = 0;
    virtual bool generateKeyPair(Bytes& privateKey, Bytes& public65) = 0;
    virtual void random(uint8_t* out, size_t n) = 0;

    Bytes randomBytes(size_t n) {
        Bytes b(n);
        random(b.data(), n);
        return b;
    }
};

}  // namespace tesla
}  // namespace solar
