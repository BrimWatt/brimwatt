#include "tesla/TeslaSelfTest.h"

#include "tesla/TeslaSession.h"

namespace solar {
namespace tesla {

namespace {

// Tesla's published example (vehicle-command, pkg/protocol/protocol.md). These keys are public test keys: never enrol them on a car.
const char* kClientPrivate = "2538CDC29A97C19C1E99A637D6CF4F8C970C118B56EDE1E6323E6D162C4B30DB";
const char* kClientPublic = "04b2b6bc68c2da0665ce656815594996c62394edd8bea905fe781a754fe6a845a714330902f225e9269d466e05b349981fda9d85cc23c6fb444aa73b629105dc6e";
const char* kVehiclePublic = "04c7a1f47138486aa4729971494878d33b1a24e39571f748a6e16c5955b3d877d3a6aaa0e955166474af5d32c410f439a2234137ad1bb085fd4e8813c958f11d97";
const char* kKey = "1b2fce19967b79db696f909cff89ea9a";
const char* kSessionInfoKey = "fceb679ee7bca756fcd441bf238bf2f338629b41d9eb9c67be1b32c9672ce300";
const char* kVin = "5YJ30123456789ABC";
const char* kChallenge = "1588d5a30eabc6f8fc9a951b11f6fd11";
const char* kHandshakeMetadata = "000106021135594a333031323334353637383941424306101588d5a30eabc6f8fc9a951b11f6fd11ff";
const char* kSessionInfo =
    "0806124104c7a1f47138486aa4729971494878d33b1a24e39571f748a6e16c5955b3d877d3a6aaa0e955166474af5d32c410f439a2234137ad1bb085fd4e8813c958f11d97"
    "1a104c463f9cc0d3d26906e982ed224adde6255a0a0000";
const char* kSessionInfoTag = "996c1fe38331be138f8039c194b14db2198846ed7d8251e6749284d7b32ea002";
const char* kEpoch = "4c463f9cc0d3d26906e982ed224adde6";
const char* kCommandMetadata = "000105010103021135594a333031323334353637383941424303104c463f9cc0d3d26906e982ed224adde6040400000a5f050400000007070400000002ff";
const char* kPlain = "120452020801";
const char* kNonce = "dbf79447fa156674dae1caed";
const char* kCipher = "38038e8c0f2e";
const char* kTag = "c228e0ff64991481db3a7bbc133696c5";

struct Report {
    std::string text;
    bool ok = true;
    void check(bool cond, const char* what) {
        if (cond) return;
        ok = false;
        text += std::string(text.empty() ? "" : "; ") + what;
    }
};

}  // namespace

bool runKnownAnswerTests(Crypto& c, std::string& report) {
    Report r;
    Bytes priv = fromHex(kClientPrivate), clientPub = fromHex(kClientPublic), vehiclePub = fromHex(kVehiclePublic);

    Bytes derived;
    r.check(c.publicKeyFor(priv, derived) && derived == clientPub, "public key from private key");

    Bytes secret;
    bool agreed = c.ecdh(priv, vehiclePub, secret);
    r.check(agreed && secret.size() == 32, "key agreement");
    Bytes key = agreed ? Session::sharedKey(c, secret) : Bytes();
    r.check(toHex(key) == kKey, "shared key");
    r.check(toHex(c.hmacSha256(key, toBytes("session info"))) == kSessionInfoKey, "session-info key (HMAC-SHA256)");

    Metadata hm;
    hm.addByte(kTagSignatureType, kSigHmac);
    hm.add(kTagPersonalization, toBytes(kVin));
    hm.add(kTagChallenge, fromHex(kChallenge));
    r.check(toHex(hm.finish()) == kHandshakeMetadata, "handshake metadata layout");

    // The car's answer to the handshake must verify and yield the session's counter, epoch and clock.
    Session s(c, priv, clientPub, kVin, kDomainInfotainment);
    s.setChallengeForTest(fromHex(kChallenge));
    Routable reply;
    reply.sessionInfo = fromHex(kSessionInfo);
    reply.sessionInfoTag = fromHex(kSessionInfoTag);
    std::string error;
    bool accepted = s.acceptSessionInfo(reply, 0, error);
    r.check(accepted, "session-info check");
    r.check(accepted && s.info().counter == 6 && s.info().clockTime == 2650 && toHex(s.info().epoch) == kEpoch, "session-info values");

    Bytes bad = reply.sessionInfoTag;
    bad[0] ^= 1;
    Session s2(c, priv, clientPub, kVin, kDomainInfotainment);
    s2.setChallengeForTest(fromHex(kChallenge));
    reply.sessionInfoTag = bad;
    r.check(!s2.acceptSessionInfo(reply, 0, error), "a damaged session-info tag is refused");

    // One command: the signed prefix and the encryption.
    Bytes meta = s.commandMetadata(fromHex(kEpoch), 2655, 7, 2);
    r.check(toHex(meta) == kCommandMetadata, "command metadata layout");
    Bytes cipher, tag;
    bool sealed = c.aesGcmEncrypt(key, fromHex(kNonce), c.sha256(meta), fromHex(kPlain), cipher, tag);
    r.check(sealed && toHex(cipher) == kCipher && toHex(tag) == kTag, "AES-GCM command");
    Bytes plain;
    r.check(c.aesGcmDecrypt(key, fromHex(kNonce), c.sha256(meta), cipher, tag, plain) && toHex(plain) == kPlain, "AES-GCM decrypt");
    tag[0] ^= 1;
    r.check(!c.aesGcmDecrypt(key, fromHex(kNonce), c.sha256(meta), cipher, tag, plain), "AES-GCM refuses a damaged tag");

    // Our own key pair: what we generate must agree with itself.
    Bytes p1, q1, p2, q2, s1, s2x;
    r.check(c.generateKeyPair(p1, q1) && c.generateKeyPair(p2, q2) && q1.size() == 65 && p1 != p2, "key generation");
    r.check(c.ecdh(p1, q2, s1) && c.ecdh(p2, q1, s2x) && s1 == s2x, "two generated keys agree");

    report = r.text;
    return r.ok;
}

}  // namespace tesla
}  // namespace solar
