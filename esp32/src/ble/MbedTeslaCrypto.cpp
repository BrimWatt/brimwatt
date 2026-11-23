#include "ble/MbedTeslaCrypto.h"

#include <esp_random.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecp.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>

namespace device {

using solar::tesla::Bytes;

namespace {

int espRng(void*, unsigned char* out, size_t n) {
    esp_fill_random(out, n);
    return 0;
}

/** The P-256 curve, loaded once per operation and released again. */
struct Curve {
    mbedtls_ecp_group grp;
    bool ok;
    Curve() {
        mbedtls_ecp_group_init(&grp);
        ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0;
    }
    ~Curve() { mbedtls_ecp_group_free(&grp); }
};

bool writePoint(const mbedtls_ecp_group& grp, const mbedtls_ecp_point& q, Bytes& out) {
    out.assign(65, 0);
    size_t n = 0;
    return mbedtls_ecp_point_write_binary(&grp, &q, MBEDTLS_ECP_PF_UNCOMPRESSED, &n, out.data(), out.size()) == 0 && n == 65;
}

}  // namespace

Bytes MbedTeslaCrypto::sha256(const Bytes& data) {
    Bytes out(32);
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), data.data(), data.size(), out.data());
    return out;
}

Bytes MbedTeslaCrypto::hmacSha256(const Bytes& key, const Bytes& data) {
    Bytes out(32);
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key.data(), key.size(), data.data(), data.size(), out.data());
    return out;
}

bool MbedTeslaCrypto::aesGcmEncrypt(const Bytes& key, const Bytes& nonce, const Bytes& aad, const Bytes& plain, Bytes& cipher, Bytes& tag) {
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    cipher.assign(plain.size(), 0);
    tag.assign(16, 0);
    uint8_t none = 0;
    bool ok = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key.data(), static_cast<unsigned>(key.size() * 8)) == 0 &&
              mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, plain.size(), nonce.data(), nonce.size(), aad.empty() ? &none : aad.data(), aad.size(),
                                        plain.empty() ? &none : plain.data(), cipher.empty() ? &none : cipher.data(), 16, tag.data()) == 0;
    mbedtls_gcm_free(&ctx);
    return ok;
}

bool MbedTeslaCrypto::aesGcmDecrypt(const Bytes& key, const Bytes& nonce, const Bytes& aad, const Bytes& cipher, const Bytes& tag, Bytes& plain) {
    if (tag.size() != 16) return false;
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    plain.assign(cipher.size(), 0);
    uint8_t none = 0;
    bool ok = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key.data(), static_cast<unsigned>(key.size() * 8)) == 0 &&
              mbedtls_gcm_auth_decrypt(&ctx, cipher.size(), nonce.data(), nonce.size(), aad.empty() ? &none : aad.data(), aad.size(), tag.data(), 16,
                                       cipher.empty() ? &none : cipher.data(), plain.empty() ? &none : plain.data()) == 0;
    mbedtls_gcm_free(&ctx);
    if (!ok) plain.clear();
    return ok;
}

bool MbedTeslaCrypto::ecdh(const Bytes& priv, const Bytes& peer, Bytes& secretX) {
    Curve curve;
    if (!curve.ok || priv.size() != 32 || peer.size() != 65) return false;
    mbedtls_mpi d, z;
    mbedtls_ecp_point q;
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&z);
    mbedtls_ecp_point_init(&q);
    secretX.assign(32, 0);
    bool ok = mbedtls_mpi_read_binary(&d, priv.data(), priv.size()) == 0 && mbedtls_ecp_point_read_binary(&curve.grp, &q, peer.data(), peer.size()) == 0 &&
              mbedtls_ecp_check_pubkey(&curve.grp, &q) == 0 && mbedtls_ecdh_compute_shared(&curve.grp, &z, &q, &d, espRng, nullptr) == 0 &&
              mbedtls_mpi_write_binary(&z, secretX.data(), 32) == 0;
    mbedtls_mpi_free(&d);
    mbedtls_mpi_free(&z);
    mbedtls_ecp_point_free(&q);
    return ok;
}

bool MbedTeslaCrypto::publicKeyFor(const Bytes& priv, Bytes& pub) {
    Curve curve;
    if (!curve.ok || priv.size() != 32) return false;
    mbedtls_mpi d;
    mbedtls_ecp_point q;
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&q);
    bool ok = mbedtls_mpi_read_binary(&d, priv.data(), priv.size()) == 0 && mbedtls_ecp_mul(&curve.grp, &q, &d, &curve.grp.G, espRng, nullptr) == 0 &&
              writePoint(curve.grp, q, pub);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&q);
    return ok;
}

bool MbedTeslaCrypto::generateKeyPair(Bytes& priv, Bytes& pub) {
    Curve curve;
    if (!curve.ok) return false;
    mbedtls_mpi d;
    mbedtls_ecp_point q;
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&q);
    priv.assign(32, 0);
    bool ok = mbedtls_ecp_gen_keypair(&curve.grp, &d, &q, espRng, nullptr) == 0 && mbedtls_mpi_write_binary(&d, priv.data(), 32) == 0 &&
              writePoint(curve.grp, q, pub);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&q);
    return ok;
}

void MbedTeslaCrypto::random(uint8_t* out, size_t n) { esp_fill_random(out, n); }

}  // namespace device
