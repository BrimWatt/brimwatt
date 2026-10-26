#include "tesla/TeslaSession.h"

#include "tesla/TeslaBle.h"

namespace solar {
namespace tesla {

namespace {

bool sameBytes(const Bytes& a, const Bytes& b) {
    if (a.size() != b.size()) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < a.size(); i++) diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    return diff == 0;
}

Bytes cat(const Bytes& a, const Bytes& b) {
    Bytes out = a;
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

}  // namespace

Bytes Session::sharedKey(Crypto& c, const Bytes& secretX) {
    std::vector<uint8_t> digest = sha1(std::string(secretX.begin(), secretX.end()));
    (void)c;
    return Bytes(digest.begin(), digest.begin() + 16);
}

Routable Session::handshakeRequest() {
    challenge_ = c_.randomBytes(16);
    Routable r;
    r.toDomain = domain_;
    r.hasSessionInfoRequest = true;
    r.requestPublicKey = pub_;
    r.requestChallenge = challenge_;
    r.uuid = challenge_;
    ready_ = false;
    keyUnknown_ = false;
    return r;
}

Bytes Session::sessionInfoMac(const Bytes& encodedInfo, const Bytes& challenge) const {
    Metadata m;
    m.addByte(kTagSignatureType, kSigHmac);
    m.add(kTagPersonalization, toBytes(vin_));
    m.add(kTagChallenge, challenge);
    return c_.hmacSha256(sessionInfoKey_, cat(m.finish(), encodedInfo));
}

bool Session::acceptSessionInfo(const Routable& reply, uint32_t nowMs, std::string& error) {
    ready_ = false;
    if (reply.hasStatus && reply.statusFault != 0) {
        error = messageFaultText(reply.statusFault);
        return false;
    }
    if (reply.sessionInfo.empty() || reply.sessionInfoTag.empty()) {
        error = "the car did not send session information";
        return false;
    }
    SessionInfo info;
    if (!decodeSessionInfo(reply.sessionInfo, info)) {
        error = "the car's session information could not be read";
        return false;
    }
    Bytes secret;
    if (!c_.ecdh(priv_, info.publicKey, secret)) {
        error = "could not agree a key with the car";
        return false;
    }
    key_ = sharedKey(c_, secret);
    sessionInfoKey_ = c_.hmacSha256(key_, toBytes("session info"));
    if (!sameBytes(sessionInfoMac(reply.sessionInfo, challenge_), reply.sessionInfoTag)) {
        error = "the car's answer did not check out (wrong VIN, or not the car we asked)";
        return false;
    }
    info_ = info;
    keyUnknown_ = info.status == 1;
    counter_ = info.counter;
    infoAtMs_ = nowMs;
    lastReplyCounter_ = 0;
    ready_ = !keyUnknown_;
    if (keyUnknown_) error = "the car does not have our key: pair again";
    return ready_;
}

Bytes Session::commandMetadata(const Bytes& epoch, uint32_t expiresAt, uint32_t counter, uint32_t flags) const {
    Metadata m;
    m.addByte(kTagSignatureType, kSigAesGcmPersonalized);
    m.addByte(kTagDomain, domain_);
    m.add(kTagPersonalization, toBytes(vin_));
    m.add(kTagEpoch, epoch);
    m.addU32(kTagExpiresAt, expiresAt);
    m.addU32(kTagCounter, counter);
    if (flags) m.addU32(kTagFlags, flags);
    return m.finish();
}

Routable Session::command(const Bytes& payload, uint32_t flags, uint32_t ttlSeconds, uint32_t nowMs, Bytes& requestTag) {
    counter_++;
    uint32_t carClock = info_.clockTime + (nowMs - infoAtMs_) / 1000;
    uint32_t expiresAt = carClock + ttlSeconds;
    Bytes aad = c_.sha256(commandMetadata(info_.epoch, expiresAt, counter_, flags));
    Bytes nonce = c_.randomBytes(12), cipher, tag;
    c_.aesGcmEncrypt(key_, nonce, aad, payload, cipher, tag);
    requestTag = tag;
    Routable r;
    r.toDomain = domain_;
    r.hasPayload = true;
    r.payload = cipher;
    r.signerPublicKey = pub_;
    r.hasGcm = true;
    r.gcmEpoch = info_.epoch;
    r.gcmNonce = nonce;
    r.gcmCounter = counter_;
    r.gcmExpiresAt = expiresAt;
    r.gcmTag = tag;
    r.flags = flags;
    r.uuid = c_.randomBytes(16);
    return r;
}

bool Session::openReply(const Routable& reply, const Bytes& requestTag, Bytes& plain, std::string& error) {
    if (reply.hasStatus && reply.statusFault != 0) {
        error = messageFaultText(reply.statusFault);
        return false;
    }
    if (!reply.hasGcmResponse || !reply.hasPayload) {
        error = "the car's reply was not encrypted as expected";
        return false;
    }
    if (reply.respCounter <= lastReplyCounter_) {
        error = "the car's reply repeated an old counter";
        return false;
    }
    Metadata m;
    m.addByte(kTagSignatureType, kSigAesGcmResponse);
    m.addByte(kTagDomain, domain_);
    m.add(kTagPersonalization, toBytes(vin_));
    m.addU32(kTagCounter, reply.respCounter);
    m.addU32(kTagFlags, reply.flags);
    Bytes requestHash;
    requestHash.push_back(static_cast<uint8_t>(kSigAesGcmPersonalized));
    requestHash.insert(requestHash.end(), requestTag.begin(), requestTag.end());
    m.add(kTagRequestHash, requestHash);
    m.addU32(kTagFault, reply.hasStatus ? static_cast<uint32_t>(reply.statusFault) : 0);
    Bytes aad = c_.sha256(m.finish());
    if (!c_.aesGcmDecrypt(key_, reply.respNonce, aad, reply.payload, reply.respTag, plain)) {
        error = "the car's reply could not be verified";
        return false;
    }
    lastReplyCounter_ = reply.respCounter;
    return true;
}

}  // namespace tesla
}  // namespace solar
