// Host-side tests for the Tesla pairing and status protocol: the wire format, Tesla's published known-answer values, and a
// pretend car that answers the whole conversation. Uses OpenSSL on the computer (see scripts/native_openssl.py).
#include "../support/OpenSslCrypto.h"
#include "../support/TestSupport.h"

#include "tesla/TeslaFlows.h"
#include "tesla/TeslaSelfTest.h"

using namespace solar::tesla;

void setUp() {}
void tearDown() {}

static testing::OpenSslCrypto crypto;

static std::string hex(const Bytes& b) { return toHex(b); }

// ---- wire format -------------------------------------------------------------------------------------------------------------

void test_varints_and_fields_round_trip() {
    Writer inner;
    inner.varint(1, 300);
    Writer w;
    w.varint(1, 150);
    w.bytes(2, Bytes{1, 2, 3});
    w.message(3, inner);
    w.fixed32(4, 2650);
    w.message(5, Writer());
    Bytes b = w.data();
    // 150 = 96 01, 300 = ac 02, 2650 = 0x0a5a little endian = 5a 0a 00 00
    TEST_ASSERT_EQUAL_STRING("0896011203010203" "1a0308ac02" "255a0a0000" "2a00", hex(b).c_str());
    Reader r(b);
    Field f;
    TEST_ASSERT_TRUE(r.next(f));
    TEST_ASSERT_EQUAL(1, f.number);
    TEST_ASSERT_EQUAL(150, (int)f.value);
    TEST_ASSERT_TRUE(r.next(f));
    TEST_ASSERT_EQUAL(3, (int)f.len);
    TEST_ASSERT_TRUE(r.next(f));
    TEST_ASSERT_TRUE(r.next(f));
    TEST_ASSERT_EQUAL(2650, (int)f.value);
    TEST_ASSERT_TRUE(r.next(f));
    TEST_ASSERT_EQUAL(0, (int)f.len);
    TEST_ASSERT_FALSE(r.next(f));
    TEST_ASSERT_TRUE(r.ok());
}

void test_damaged_bytes_are_reported_not_crashed_on() {
    Bytes cut = fromHex("1205010203");   // says 5 bytes, has 3
    Reader r(cut);
    Field f;
    TEST_ASSERT_FALSE(r.next(f));
    TEST_ASSERT_FALSE(r.ok());
    Routable out;
    TEST_ASSERT_FALSE(decodeRoutable(cut, out));
}

void test_the_add_key_message_has_the_published_layout() {
    // PermissionChange: key = 1, keyRole = 4. WhitelistOperation: addKeyToWhitelistAndAddPermissions = 5, metadataForKey = 6.
    Bytes m = vcsecAddKey(Bytes{0xaa, 0xbb}, kRoleChargingManager, kFormFactorCloudKey);
    TEST_ASSERT_EQUAL_STRING("0a15" "1211" "82010e" "2a08" "0a040a02aabb" "2006" "3202" "0809" "1802", hex(m).c_str());
}

void test_status_and_charge_requests_are_short_and_change_nothing() {
    TEST_ASSERT_EQUAL_STRING("0a0412020a00", hex(vcsecGetStatus()).c_str());
    TEST_ASSERT_EQUAL_STRING("12040a021200", hex(carGetChargeState()).c_str());
}

void test_routable_round_trip() {
    Routable r;
    r.toDomain = kDomainInfotainment;
    r.fromRouting = Bytes(16, 7);
    r.hasPayload = true;
    r.payload = Bytes{9, 8, 7};
    r.signerPublicKey = Bytes(65, 4);
    r.hasGcm = true;
    r.gcmEpoch = Bytes(16, 1);
    r.gcmNonce = Bytes(12, 2);
    r.gcmCounter = 7;
    r.gcmExpiresAt = 2655;
    r.gcmTag = Bytes(16, 3);
    r.flags = 2;
    r.uuid = Bytes(16, 5);
    Routable back;
    TEST_ASSERT_TRUE(decodeRoutable(encodeRoutable(r), back));
    TEST_ASSERT_EQUAL(kDomainInfotainment, back.toDomain);
    TEST_ASSERT_TRUE(back.fromRouting == r.fromRouting);
    TEST_ASSERT_TRUE(back.payload == r.payload);
    TEST_ASSERT_TRUE(back.hasGcm);
    TEST_ASSERT_EQUAL(7, (int)back.gcmCounter);
    TEST_ASSERT_EQUAL(2655, (int)back.gcmExpiresAt);
    TEST_ASSERT_TRUE(back.gcmTag == r.gcmTag);
    TEST_ASSERT_EQUAL(2, (int)back.flags);
    TEST_ASSERT_TRUE(back.signerPublicKey == r.signerPublicKey);
}

// ---- Tesla's published example values ----------------------------------------------------------------------------------------

void test_known_answers_from_teslas_protocol_description() {
    std::string report;
    bool ok = runKnownAnswerTests(crypto, report);
    TEST_ASSERT_TRUE_MESSAGE(ok, report.c_str());
}

// ---- reading the car's replies -----------------------------------------------------------------------------------------------

static Bytes fromVcsecStatus(int lock, int sleep, int presence) {
    Writer s;
    if (lock) s.varint(2, static_cast<uint64_t>(lock));
    s.varint(3, static_cast<uint64_t>(sleep));
    s.varint(4, static_cast<uint64_t>(presence));
    Writer w;
    w.message(1, s);
    return w.data();
}

static Bytes fromVcsecCommand(int op, int info, bool whitelist) {
    Writer cs;
    if (op) cs.varint(1, static_cast<uint64_t>(op));
    if (whitelist) {
        Writer wl;
        if (info) wl.varint(1, static_cast<uint64_t>(info));
        cs.message(3, wl);
    }
    Writer w;
    w.message(4, cs);
    return w.data();
}

static Bytes routableWith(const Bytes& payload) {
    Routable r;
    r.hasPayload = true;
    r.payload = payload;
    return encodeRoutable(r);
}

void test_vehicle_status_is_read() {
    VcsecReply v;
    TEST_ASSERT_TRUE(decodeVcsecReply(fromVcsecStatus(1, 2, 1), v));
    TEST_ASSERT_TRUE(v.isVehicleStatus);
    TEST_ASSERT_EQUAL(1, v.lockState);
    TEST_ASSERT_EQUAL(2, v.sleepStatus);
    TEST_ASSERT_EQUAL_STRING("asleep", sleepText(v.sleepStatus));
    TEST_ASSERT_EQUAL_STRING("locked", lockText(v.lockState));
}

void test_charge_state_is_read() {
    Writer cs, charging, charge, data, response;
    charging.message(5, Writer());           // Charging
    cs.message(1, charging);
    cs.varint(114, 76);
    cs.varint(104, 80);
    cs.varint(121, 24);
    cs.varint(137, 32);
    cs.varint(138, 32);
    cs.varint(122, 7);
    cs.varint(119, 240);
    data.message(3, cs);
    response.message(2, data);
    CarReply r;
    TEST_ASSERT_TRUE(decodeCarReply(response.data(), r));
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.charge.valid);
    TEST_ASSERT_EQUAL(5, r.charge.chargingState);
    TEST_ASSERT_EQUAL(76, r.charge.batteryLevel);
    TEST_ASSERT_EQUAL(80, r.charge.chargeLimit);
    TEST_ASSERT_EQUAL(24, r.charge.actualCurrent);
    TEST_ASSERT_EQUAL(32, r.charge.currentRequest);
    TEST_ASSERT_EQUAL(240, r.charge.voltage);
    TEST_ASSERT_EQUAL_STRING("charging", chargingStateText(r.charge.chargingState));
}

void test_an_error_reply_carries_its_reason() {
    Writer reason, st, response;
    reason.text(1, "not allowed");
    st.varint(1, 1);
    st.message(2, reason);
    response.message(1, st);
    CarReply r;
    TEST_ASSERT_TRUE(decodeCarReply(response.data(), r));
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("not allowed", r.reasonText.c_str());
}

// ---- pairing ------------------------------------------------------------------------------------------------------------------

void test_pairing_asks_for_the_charging_manager_role_and_waits_for_the_tap() {
    Bytes pub(65, 4);
    PairFlow flow(crypto, pub, kRoleChargingManager);
    Routable sent;
    TEST_ASSERT_TRUE(decodeRoutable(flow.request(), sent));
    TEST_ASSERT_EQUAL(kDomainVehicleSecurity, sent.toDomain);
    TEST_ASSERT_TRUE(sent.payload == vcsecAddKey(pub, kRoleChargingManager, kFormFactorCloudKey));
    flow.onMessage(routableWith(fromVcsecCommand(1, 0, false)));        // WAIT
    TEST_ASSERT_EQUAL(PairFlow::Waiting, flow.state());
    TEST_ASSERT_TRUE(flow.text().find("key card") != std::string::npos);
    flow.onMessage(routableWith(fromVcsecCommand(0, 0, true)));         // OK with an empty whitelist status
    TEST_ASSERT_EQUAL(PairFlow::Accepted, flow.state());
}

void test_pairing_explains_a_refusal() {
    PairFlow flow(crypto, Bytes(65, 4), kRoleChargingManager);
    flow.request();
    flow.onMessage(routableWith(fromVcsecCommand(1, 0, false)));
    flow.onMessage(routableWith(fromVcsecCommand(2, 25, true)));        // key card not tapped in time
    TEST_ASSERT_EQUAL(PairFlow::Refused, flow.state());
    TEST_ASSERT_TRUE(flow.text().find("key card was not tapped in time") != std::string::npos);
    flow.onMessage(routableWith(fromVcsecCommand(0, 0, true)));         // a late message must not undo it
    TEST_ASSERT_EQUAL(PairFlow::Refused, flow.state());
}

void test_pairing_again_with_the_same_key_counts_as_paired() {
    PairFlow flow(crypto, Bytes(65, 4), kRoleChargingManager);
    flow.request();
    flow.onMessage(routableWith(fromVcsecCommand(2, 13, true)));
    TEST_ASSERT_EQUAL(PairFlow::Refused, flow.state());
    TEST_ASSERT_TRUE(flow.alreadyOnCar());
    TEST_ASSERT_TRUE(flow.text().find("already on the car") != std::string::npos);
}

void test_pairing_ignores_noise_and_reports_a_message_fault() {
    PairFlow flow(crypto, Bytes(65, 4), kRoleChargingManager);
    flow.request();
    flow.onMessage(Bytes{0xff, 0xff});
    flow.onMessage(routableWith(fromVcsecStatus(1, 1, 1)));             // some other message
    TEST_ASSERT_EQUAL(PairFlow::Ready, flow.state());
    Routable r;
    r.hasStatus = true;
    r.statusOperation = 2;
    r.statusFault = 7;
    // statusFault is written by the car; build the bytes by hand: signedMessageStatus (12) { op = 2, fault = 7 }
    Writer st;
    st.varint(1, 2);
    st.varint(2, 7);
    Writer w;
    w.message(12, st);
    flow.onMessage(w.data());
    TEST_ASSERT_EQUAL(PairFlow::Refused, flow.state());
    TEST_ASSERT_TRUE(flow.text().find("role") != std::string::npos);
}

// ---- a pretend car ------------------------------------------------------------------------------------------------------------

class FakeCar {
public:
    Bytes priv, pub, clientPub;
    std::string vin = "5YJ30123456789ABC";
    int sleep = 1;
    bool knowsKey = true;
    bool encryptsReply = true;
    int chargeState = 5;
    Bytes key;
    Bytes epoch = Bytes(16, 0x42);
    uint32_t counter = 5;
    uint32_t clock = 1000;
    uint32_t replyCounter = 0;
    int commandsSeen = 0;
    Bytes lastPlain;

    FakeCar() { crypto.generateKeyPair(priv, pub); }

    Bytes answer(const Bytes& bytes) {
        Routable in;
        if (!decodeRoutable(bytes, in)) return Bytes();
        Routable out;
        out.toRouting = in.fromRouting;
        out.requestUuid = in.uuid;
        if (in.toDomain == kDomainVehicleSecurity) {
            out.hasPayload = true;
            out.payload = fromVcsecStatus(1, sleep, 1);
            return encodeRoutable(out);
        }
        if (in.hasSessionInfoRequest) {
            clientPub = in.requestPublicKey;
            Bytes secret;
            crypto.ecdh(priv, clientPub, secret);
            key = Session::sharedKey(crypto, secret);
            Writer info;
            info.varint(1, counter);
            info.bytes(2, pub);
            info.bytes(3, epoch);
            info.fixed32(4, clock);
            if (!knowsKey) info.varint(5, 1);
            Bytes encoded = info.data();
            Metadata m;
            m.addByte(kTagSignatureType, kSigHmac);
            m.add(kTagPersonalization, toBytes(vin));
            m.add(kTagChallenge, in.requestChallenge);
            Bytes macKey = crypto.hmacSha256(key, toBytes("session info"));
            Bytes data = m.finish();
            data.insert(data.end(), encoded.begin(), encoded.end());
            out.sessionInfo = encoded;
            out.sessionInfoTag = crypto.hmacSha256(macKey, data);
            return encodeRoutable(out);
        }
        if (in.hasGcm) {
            commandsSeen++;
            Metadata m;
            m.addByte(kTagSignatureType, kSigAesGcmPersonalized);
            m.addByte(kTagDomain, in.toDomain);
            m.add(kTagPersonalization, toBytes(vin));
            m.add(kTagEpoch, in.gcmEpoch);
            m.addU32(kTagExpiresAt, in.gcmExpiresAt);
            m.addU32(kTagCounter, in.gcmCounter);
            if (in.flags) m.addU32(kTagFlags, in.flags);
            Bytes plain;
            if (!crypto.aesGcmDecrypt(key, in.gcmNonce, crypto.sha256(m.finish()), in.payload, in.gcmTag, plain)) return Bytes();
            lastPlain = plain;
            if (in.gcmCounter <= counter) return Bytes();
            counter = in.gcmCounter;
            Writer cs, charging, data, response;
            charging.message(static_cast<int>(chargeState), Writer());
            cs.message(1, charging);
            cs.varint(114, 76);
            cs.varint(104, 80);
            cs.varint(121, 24);
            cs.varint(137, 32);
            response.message(2, data);
            data.message(3, cs);
            Writer response2;
            response2.message(2, data);
            Bytes body = response2.data();
            replyCounter++;
            Metadata rm;
            rm.addByte(kTagSignatureType, kSigAesGcmResponse);
            rm.addByte(kTagDomain, in.toDomain);
            rm.add(kTagPersonalization, toBytes(vin));
            rm.addU32(kTagCounter, replyCounter);
            rm.addU32(kTagFlags, in.flags);
            Bytes hash{static_cast<uint8_t>(kSigAesGcmPersonalized)};
            hash.insert(hash.end(), in.gcmTag.begin(), in.gcmTag.end());
            rm.add(kTagRequestHash, hash);
            rm.addU32(kTagFault, 0);
            Bytes nonce = crypto.randomBytes(12), cipher, tag;
            crypto.aesGcmEncrypt(key, nonce, crypto.sha256(rm.finish()), body, cipher, tag);
            out.hasPayload = true;
            out.payload = cipher;
            out.hasGcmResponse = true;
            out.respNonce = nonce;
            out.respCounter = replyCounter;
            out.respTag = tag;
            out.flags = in.flags;
            return encodeRoutable(out);
        }
        return Bytes();
    }
};

static Bytes drive(StatusFlow& flow, FakeCar& car) {
    Bytes msg = flow.start();
    uint32_t now = 10000;
    for (int guard = 0; guard < 8 && !msg.empty() && !flow.finished(); guard++) {
        Bytes reply = car.answer(msg);
        if (reply.empty()) return msg;
        msg = flow.onMessage(reply, now += 200);
    }
    return Bytes();
}

void test_status_reads_the_charge_state_from_an_awake_car() {
    FakeCar car;
    Bytes priv, pub;
    crypto.generateKeyPair(priv, pub);
    StatusFlow flow(crypto, priv, pub, car.vin);
    drive(flow, car);
    TEST_ASSERT_TRUE(flow.finished());
    std::string all;
    for (auto& l : flow.lines()) all += l + "\n";
    TEST_ASSERT_TRUE_MESSAGE(flow.ok(), all.c_str());
    TEST_ASSERT_EQUAL(76, flow.charge().batteryLevel);
    TEST_ASSERT_EQUAL(80, flow.charge().chargeLimit);
    TEST_ASSERT_EQUAL(24, flow.charge().actualCurrent);
    TEST_ASSERT_EQUAL(5, flow.charge().chargingState);
    TEST_ASSERT_EQUAL(1, car.commandsSeen);
    TEST_ASSERT_TRUE(all.find("Battery: 76 %") != std::string::npos);
    // the one command the car saw was the read-only charge-state request
    TEST_ASSERT_EQUAL_STRING(hex(carGetChargeState()).c_str(), hex(car.lastPlain).c_str());
}

void test_status_does_not_wake_a_sleeping_car() {
    FakeCar car;
    car.sleep = 2;
    Bytes priv, pub;
    crypto.generateKeyPair(priv, pub);
    StatusFlow flow(crypto, priv, pub, car.vin);
    drive(flow, car);
    TEST_ASSERT_TRUE(flow.finished());
    TEST_ASSERT_TRUE(flow.ok());
    TEST_ASSERT_EQUAL(0, car.commandsSeen);
    TEST_ASSERT_EQUAL(0, (int)car.key.size());     // not even a session was opened
    TEST_ASSERT_TRUE(flow.lines()[0].find("asleep") != std::string::npos);
}

void test_status_says_so_when_the_car_does_not_know_our_key() {
    FakeCar car;
    car.knowsKey = false;
    Bytes priv, pub;
    crypto.generateKeyPair(priv, pub);
    StatusFlow flow(crypto, priv, pub, car.vin);
    drive(flow, car);
    TEST_ASSERT_TRUE(flow.finished());
    TEST_ASSERT_FALSE(flow.ok());
    TEST_ASSERT_TRUE(flow.lines().back().find("Pair again") != std::string::npos);
    TEST_ASSERT_EQUAL(0, car.commandsSeen);
}

void test_status_with_the_wrong_vin_is_refused_not_trusted() {
    FakeCar car;
    Bytes priv, pub;
    crypto.generateKeyPair(priv, pub);
    StatusFlow flow(crypto, priv, pub, "5YJ30123456789ABD");   // not the car's VIN
    drive(flow, car);
    TEST_ASSERT_TRUE(flow.finished());
    TEST_ASSERT_FALSE(flow.ok());
    TEST_ASSERT_EQUAL(0, car.commandsSeen);
    TEST_ASSERT_TRUE(flow.lines().back().find("VIN") != std::string::npos);
}

void test_a_silent_car_ends_the_status_with_a_plain_message() {
    Bytes priv, pub;
    crypto.generateKeyPair(priv, pub);
    StatusFlow flow(crypto, priv, pub, "5YJ30123456789ABC");
    flow.start();
    flow.timedOut();
    TEST_ASSERT_TRUE(flow.finished());
    TEST_ASSERT_FALSE(flow.ok());
    TEST_ASSERT_TRUE(flow.lines()[0].find("did not answer") != std::string::npos);
}

void test_a_reply_that_fails_its_check_is_not_shown() {
    FakeCar car;
    Bytes priv, pub;
    crypto.generateKeyPair(priv, pub);
    StatusFlow flow(crypto, priv, pub, car.vin);
    Bytes msg = flow.start();
    msg = flow.onMessage(car.answer(msg), 10000);          // status -> handshake request
    msg = flow.onMessage(car.answer(msg), 10200);          // session info -> command
    Bytes reply = car.answer(msg);
    Routable r;
    decodeRoutable(reply, r);
    r.payload[0] ^= 1;                                     // damaged in the air
    flow.onMessage(encodeRoutable(r), 10400);
    TEST_ASSERT_TRUE(flow.finished());
    TEST_ASSERT_FALSE(flow.ok());
    TEST_ASSERT_EQUAL(0, flow.charge().batteryLevel);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_varints_and_fields_round_trip);
    RUN_TEST(test_damaged_bytes_are_reported_not_crashed_on);
    RUN_TEST(test_the_add_key_message_has_the_published_layout);
    RUN_TEST(test_status_and_charge_requests_are_short_and_change_nothing);
    RUN_TEST(test_routable_round_trip);
    RUN_TEST(test_known_answers_from_teslas_protocol_description);
    RUN_TEST(test_vehicle_status_is_read);
    RUN_TEST(test_charge_state_is_read);
    RUN_TEST(test_an_error_reply_carries_its_reason);
    RUN_TEST(test_pairing_asks_for_the_charging_manager_role_and_waits_for_the_tap);
    RUN_TEST(test_pairing_explains_a_refusal);
    RUN_TEST(test_pairing_again_with_the_same_key_counts_as_paired);
    RUN_TEST(test_pairing_ignores_noise_and_reports_a_message_fault);
    RUN_TEST(test_status_reads_the_charge_state_from_an_awake_car);
    RUN_TEST(test_status_does_not_wake_a_sleeping_car);
    RUN_TEST(test_status_says_so_when_the_car_does_not_know_our_key);
    RUN_TEST(test_status_with_the_wrong_vin_is_refused_not_trusted);
    RUN_TEST(test_a_silent_car_ends_the_status_with_a_plain_message);
    RUN_TEST(test_a_reply_that_fails_its_check_is_not_shown);
    return UNITY_END();
}
