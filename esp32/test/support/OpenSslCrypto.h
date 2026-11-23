#pragma once

// Host-test implementation of the Tesla Crypto interface on OpenSSL (the firmware uses mbedTLS instead).
// Needs OpenSSL on the computer: scripts/native_openssl.py finds Homebrew's.
#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>

#include <memory>

#include "tesla/TeslaCrypto.h"

namespace testing {

class OpenSslCrypto : public solar::tesla::Crypto {
public:
    using Bytes = solar::tesla::Bytes;

    Bytes sha256(const Bytes& data) override {
        Bytes out(32);
        unsigned int n = 0;
        EVP_Digest(data.data(), data.size(), out.data(), &n, EVP_sha256(), nullptr);
        return out;
    }

    Bytes hmacSha256(const Bytes& key, const Bytes& data) override {
        Bytes out(32);
        unsigned int n = 0;
        HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), data.data(), data.size(), out.data(), &n);
        return out;
    }

    bool aesGcmEncrypt(const Bytes& key, const Bytes& nonce, const Bytes& aad, const Bytes& plain, Bytes& cipher, Bytes& tag) override {
        std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
        int len = 0;
        cipher.assign(plain.size() + 16, 0);
        tag.assign(16, 0);
        if (!EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_gcm(), nullptr, nullptr, nullptr)) return false;
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr);
        if (!EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce.data())) return false;
        if (!aad.empty() && !EVP_EncryptUpdate(ctx.get(), nullptr, &len, aad.data(), static_cast<int>(aad.size()))) return false;
        int total = 0;
        if (!plain.empty()) {
            if (!EVP_EncryptUpdate(ctx.get(), cipher.data(), &len, plain.data(), static_cast<int>(plain.size()))) return false;
            total = len;
        }
        if (!EVP_EncryptFinal_ex(ctx.get(), cipher.data() + total, &len)) return false;
        total += len;
        cipher.resize(static_cast<size_t>(total));
        return EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 16, tag.data()) == 1;
    }

    bool aesGcmDecrypt(const Bytes& key, const Bytes& nonce, const Bytes& aad, const Bytes& cipher, const Bytes& tag, Bytes& plain) override {
        if (tag.size() != 16) return false;
        std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
        int len = 0;
        plain.assign(cipher.size() + 16, 0);
        if (!EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_gcm(), nullptr, nullptr, nullptr)) return false;
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr);
        if (!EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce.data())) return false;
        if (!aad.empty() && !EVP_DecryptUpdate(ctx.get(), nullptr, &len, aad.data(), static_cast<int>(aad.size()))) return false;
        int total = 0;
        if (!cipher.empty()) {
            if (!EVP_DecryptUpdate(ctx.get(), plain.data(), &len, cipher.data(), static_cast<int>(cipher.size()))) return false;
            total = len;
        }
        Bytes t = tag;
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, 16, t.data());
        if (EVP_DecryptFinal_ex(ctx.get(), plain.data() + total, &len) <= 0) {
            plain.clear();
            return false;
        }
        plain.resize(static_cast<size_t>(total + len));
        return true;
    }

    bool ecdh(const Bytes& priv, const Bytes& peer, Bytes& secretX) override {
        EcKey key = load(priv);
        if (!key) return false;
        EC_KEY* peerKey = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
        const EC_GROUP* group = EC_KEY_get0_group(peerKey);
        EC_POINT* point = EC_POINT_new(group);
        bool ok = EC_POINT_oct2point(group, point, peer.data(), peer.size(), nullptr) == 1;
        secretX.assign(32, 0);
        if (ok) ok = ECDH_compute_key(secretX.data(), 32, point, key.get(), nullptr) == 32;
        EC_POINT_free(point);
        EC_KEY_free(peerKey);
        return ok;
    }

    bool publicKeyFor(const Bytes& priv, Bytes& pub) override {
        EcKey key = load(priv);
        return key && encode(key.get(), pub);
    }

    bool generateKeyPair(Bytes& priv, Bytes& pub) override {
        EcKey key(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1), EC_KEY_free);
        if (!EC_KEY_generate_key(key.get())) return false;
        priv.assign(32, 0);
        BN_bn2binpad(EC_KEY_get0_private_key(key.get()), priv.data(), 32);
        return encode(key.get(), pub);
    }

    void random(uint8_t* out, size_t n) override { RAND_bytes(out, static_cast<int>(n)); }

private:
    using EcKey = std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)>;

    static EcKey load(const Bytes& priv) {
        EcKey key(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1), EC_KEY_free);
        BIGNUM* d = BN_bin2bn(priv.data(), static_cast<int>(priv.size()), nullptr);
        const EC_GROUP* group = EC_KEY_get0_group(key.get());
        EC_POINT* q = EC_POINT_new(group);
        bool ok = d && EC_KEY_set_private_key(key.get(), d) == 1 && EC_POINT_mul(group, q, d, nullptr, nullptr, nullptr) == 1 &&
                  EC_KEY_set_public_key(key.get(), q) == 1;
        EC_POINT_free(q);
        BN_free(d);
        if (!ok) key.reset();
        return key;
    }

    static bool encode(EC_KEY* key, Bytes& pub) {
        pub.assign(65, 0);
        const EC_GROUP* group = EC_KEY_get0_group(key);
        return EC_POINT_point2oct(group, EC_KEY_get0_public_key(key), POINT_CONVERSION_UNCOMPRESSED, pub.data(), 65, nullptr) == 65;
    }
};

}  // namespace testing
