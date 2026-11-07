// Host tests for "Connect through: Tesla account": base64, the token refresh (single-use refresh tokens), the Fleet API calls and their
// plain-words errors, and the car service over a pretend Tesla server. No network, no car.
#include "../support/OpenSslCrypto.h"
#include "../support/TestSupport.h"

#include "fleet/Base64.h"
#include "fleet/FleetApi.h"
#include "fleet/FleetAuth.h"
#include "fleet/FleetCar.h"

using namespace solar;
using namespace solar::fleet;
using testing::FakePlatform;

void setUp() {}
void tearDown() {}

namespace {

class MemoryStore : public FleetCredentialStore {
public:
    FleetCredentials creds;
    int saves = 0;
    bool load(FleetCredentials& out) override { out = creds; return creds.complete(); }
    void save(const FleetCredentials& in) override { creds = in; saves++; }
    void clear() override { creds = FleetCredentials(); }
};

/** A scripted Tesla: answers by URL, records what was asked. */
class FakeTesla : public FleetHttp {
public:
    std::vector<FleetHttpRequest> seen;
    bool online = true;
    int tokenStatus = 200;
    int tokenCount = 0;
    std::string vehicleBody;
    int vehicleStatus = 200;
    int wakeStatus = 200;
    int wakes = 0;
    int vehicleFirstStatus401s = 0;   // answer this many vehicle_data calls with 401 first

    FleetHttpResponse send(const FleetHttpRequest& r) override {
        seen.push_back(r);
        FleetHttpResponse out;
        if (!online) return out;
        out.sent = true;
        if (r.url.find("/oauth2/v3/token") != std::string::npos) {
            tokenCount++;
            out.status = tokenStatus;
            out.body = "{\"access_token\":\"access-" + std::to_string(tokenCount) + "\",\"refresh_token\":\"refresh-" + std::to_string(tokenCount + 1) +
                       "\",\"expires_in\":28800}";
            return out;
        }
        if (r.url.find("/wake_up") != std::string::npos) { wakes++; out.status = wakeStatus; out.body = "{\"response\":{\"state\":\"online\"}}"; return out; }
        if (r.url.find("/vehicle_data") != std::string::npos) {
            if (vehicleFirstStatus401s > 0) { vehicleFirstStatus401s--; out.status = 401; return out; }
            out.status = vehicleStatus;
            out.body = vehicleBody;
            return out;
        }
        out.status = 404;
        return out;
    }
};

class InlineExecutor : public FleetExecutor {
public:
    uint32_t slept = 0;
    bool start(std::function<void()> job) override { job(); return true; }
    void sleepMs(uint32_t ms) override { slept += ms; }
};

class FakeIdentity : public FleetIdentity {
public:
    std::string v = "5YJ30123456789ABC";
    tesla::Bytes key;
    std::string vin() override { return v; }
    bool privateKey(tesla::Bytes& out) override { if (key.empty()) return false; out = key; return true; }
};

const char* kCharging =
    "{\"response\":{\"charge_state\":{\"charging_state\":\"Charging\",\"battery_level\":61,\"charge_limit_soc\":80,"
    "\"charge_current_request\":16,\"charge_current_request_max\":32,\"charger_actual_current\":15.6,\"charger_power\":4,\"charger_voltage\":243}}}";

FleetCredentials signedIn() {
    FleetCredentials c;
    c.clientId = "client-id";
    c.refreshToken = "refresh-1";
    return c;
}

}  // namespace

// ---- base64 -----------------------------------------------------------------------------------------------------------------

void test_base64_matches_the_standard_examples() {
    TEST_ASSERT_EQUAL_STRING("", base64Encode({}).c_str());
    TEST_ASSERT_EQUAL_STRING("Zg==", base64Encode({'f'}).c_str());
    TEST_ASSERT_EQUAL_STRING("Zm8=", base64Encode({'f', 'o'}).c_str());
    TEST_ASSERT_EQUAL_STRING("Zm9v", base64Encode({'f', 'o', 'o'}).c_str());
    TEST_ASSERT_EQUAL_STRING("Zm9vYg==", base64Encode({'f', 'o', 'o', 'b'}).c_str());
    tesla::Bytes back;
    TEST_ASSERT_TRUE(base64Decode("Zm9vYg==", back));
    TEST_ASSERT_EQUAL(4, back.size());
    TEST_ASSERT_TRUE(base64Decode("Zm9vYg", back));   // padding optional
    TEST_ASSERT_EQUAL(4, back.size());
    TEST_ASSERT_FALSE(base64Decode("Zm9v!", back));
}

void test_base64_round_trips_every_byte_value() {
    tesla::Bytes all;
    for (int i = 0; i < 256; i++) all.push_back(static_cast<uint8_t>(i));
    tesla::Bytes back;
    TEST_ASSERT_TRUE(base64Decode(base64Encode(all), back));
    TEST_ASSERT_TRUE(all == back);
}

// ---- tokens -----------------------------------------------------------------------------------------------------------------

void test_not_connected_gives_no_token_and_says_so() {
    FakePlatform p; FakeTesla net; MemoryStore store;
    FleetAuth auth(net, store, p);
    TEST_ASSERT_FALSE(auth.connected());
    TEST_ASSERT_EQUAL_STRING("", auth.accessToken().c_str());
    TEST_ASSERT_TRUE(auth.state() == FleetAuth::State::NotConnected);
    TEST_ASSERT_EQUAL(0, net.seen.size());
}

void test_refresh_sends_only_the_client_id_and_the_refresh_token_and_keeps_the_new_one() {
    FakePlatform p; FakeTesla net; MemoryStore store; store.creds = signedIn();
    FleetAuth auth(net, store, p);
    TEST_ASSERT_EQUAL_STRING("access-1", auth.accessToken().c_str());
    TEST_ASSERT_EQUAL(1, net.seen.size());
    const std::string body = net.seen[0].body;
    TEST_ASSERT_TRUE(body.find("grant_type=refresh_token") != std::string::npos);
    TEST_ASSERT_TRUE(body.find("client_id=client-id") != std::string::npos);
    TEST_ASSERT_TRUE(body.find("refresh_token=refresh-1") != std::string::npos);
    TEST_ASSERT_TRUE(body.find("secret") == std::string::npos);
    TEST_ASSERT_EQUAL_STRING("refresh-2", store.creds.refreshToken.c_str());   // single use: the new one is saved at once
}

void test_a_token_is_reused_until_it_is_about_to_expire() {
    FakePlatform p; FakeTesla net; MemoryStore store; store.creds = signedIn();
    FleetAuth auth(net, store, p);
    auth.accessToken();
    p.now += 3600UL * 1000;   // an hour later: still fine (lifetime 8 h)
    TEST_ASSERT_EQUAL_STRING("access-1", auth.accessToken().c_str());
    TEST_ASSERT_EQUAL(1, net.tokenCount);
    p.now += 7UL * 3600 * 1000;   // inside the safety margin of the end
    TEST_ASSERT_EQUAL_STRING("access-2", auth.accessToken().c_str());
    TEST_ASSERT_EQUAL_STRING("refresh-3", store.creds.refreshToken.c_str());
}

void test_a_refused_refresh_token_needs_a_new_sign_in_and_is_not_hammered() {
    FakePlatform p; FakeTesla net; MemoryStore store; store.creds = signedIn();
    net.tokenStatus = 401;
    FleetAuth auth(net, store, p);
    TEST_ASSERT_EQUAL_STRING("", auth.accessToken().c_str());
    TEST_ASSERT_TRUE(auth.state() == FleetAuth::State::NeedsSignIn);
    TEST_ASSERT_TRUE(auth.error().find("Sign in again") != std::string::npos);
    auth.accessToken();
    auth.accessToken();
    TEST_ASSERT_EQUAL(1, net.tokenCount);   // it waits instead of asking again
    TEST_ASSERT_EQUAL_STRING("refresh-1", store.creds.refreshToken.c_str());   // the saved token is kept, not dropped
}

void test_no_internet_is_offline_not_signed_out_and_retries_soon() {
    FakePlatform p; FakeTesla net; MemoryStore store; store.creds = signedIn();
    net.online = false;
    FleetAuth auth(net, store, p);
    TEST_ASSERT_EQUAL_STRING("", auth.accessToken().c_str());
    TEST_ASSERT_TRUE(auth.state() == FleetAuth::State::Offline);
    net.online = true;
    TEST_ASSERT_EQUAL_STRING("", auth.accessToken().c_str());   // still inside the 30 s wait
    p.now += 31 * 1000;
    TEST_ASSERT_EQUAL_STRING("access-1", auth.accessToken().c_str());
    TEST_ASSERT_TRUE(auth.state() == FleetAuth::State::Ok);
}

void test_region_picks_the_base_url_and_forget_deletes_the_credentials() {
    FakePlatform p; FakeTesla net; MemoryStore store; store.creds = signedIn();
    FleetAuth auth(net, store, p);
    TEST_ASSERT_TRUE(auth.apiBase().find(".na.") != std::string::npos);
    FleetCredentials eu = signedIn(); eu.region = "eu";
    auth.connect(eu);
    TEST_ASSERT_TRUE(auth.apiBase().find(".eu.") != std::string::npos);
    auth.forget();
    TEST_ASSERT_FALSE(auth.connected());
    TEST_ASSERT_EQUAL_STRING("", store.creds.refreshToken.c_str());
}

// ---- the API ----------------------------------------------------------------------------------------------------------------

void test_charge_state_json_becomes_a_charge_state() {
    tesla::ChargeState c;
    TEST_ASSERT_TRUE(FleetApi::parseChargeState(kCharging, c));
    TEST_ASSERT_TRUE(c.valid);
    TEST_ASSERT_EQUAL(5, c.chargingState);
    TEST_ASSERT_EQUAL(61, c.batteryLevel);
    TEST_ASSERT_EQUAL(80, c.chargeLimit);
    TEST_ASSERT_EQUAL(16, c.currentRequest);
    TEST_ASSERT_EQUAL(32, c.currentRequestMax);
    TEST_ASSERT_EQUAL(16, c.actualCurrent);   // 15.6 rounds
    TEST_ASSERT_EQUAL(243, c.voltage);
    TEST_ASSERT_FALSE(FleetApi::parseChargeState("{\"response\":{}}", c));
    TEST_ASSERT_FALSE(FleetApi::parseChargeState("not json", c));
}

void test_every_charging_state_word_has_the_codes_the_controller_uses() {
    TEST_ASSERT_EQUAL(2, FleetApi::chargingStateCode("Disconnected"));
    TEST_ASSERT_EQUAL(3, FleetApi::chargingStateCode("NoPower"));
    TEST_ASSERT_EQUAL(4, FleetApi::chargingStateCode("Starting"));
    TEST_ASSERT_EQUAL(5, FleetApi::chargingStateCode("Charging"));
    TEST_ASSERT_EQUAL(6, FleetApi::chargingStateCode("Complete"));
    TEST_ASSERT_EQUAL(7, FleetApi::chargingStateCode("Stopped"));
    TEST_ASSERT_EQUAL(8, FleetApi::chargingStateCode("Calibrating"));
    TEST_ASSERT_EQUAL(1, FleetApi::chargingStateCode("something new"));
}

void test_reading_asks_for_the_charge_state_only_with_a_bearer_token_and_never_wakes() {
    FakePlatform p; FakeTesla net; MemoryStore store; store.creds = signedIn(); net.vehicleBody = kCharging;
    FleetAuth auth(net, store, p); FleetApi api(net, auth);
    const FleetRead r = api.readCharge("5YJ30123456789ABC");
    TEST_ASSERT_TRUE(r.ok);
    const FleetHttpRequest& req = net.seen.back();
    TEST_ASSERT_EQUAL_STRING("GET", req.method.c_str());
    TEST_ASSERT_TRUE(req.url.find("fleet-api.prd.na") != std::string::npos);
    TEST_ASSERT_TRUE(req.url.find("/vehicles/5YJ30123456789ABC/vehicle_data?endpoints=charge_state") != std::string::npos);
    TEST_ASSERT_EQUAL_STRING("Authorization", req.headers[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("Bearer access-1", req.headers[0].second.c_str());
    TEST_ASSERT_EQUAL(0, net.wakes);
}

void test_a_sleeping_car_is_an_answer_not_an_error() {
    FakePlatform p; FakeTesla net; MemoryStore store; store.creds = signedIn(); net.vehicleStatus = 408;
    FleetAuth auth(net, store, p); FleetApi api(net, auth);
    const FleetRead r = api.readCharge("5YJ30123456789ABC");
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(r.asleep);
    TEST_ASSERT_EQUAL(0, net.wakes);
}

void test_a_401_gets_one_new_token_and_one_retry() {
    FakePlatform p; FakeTesla net; MemoryStore store; store.creds = signedIn(); net.vehicleBody = kCharging; net.vehicleFirstStatus401s = 1;
    FleetAuth auth(net, store, p); FleetApi api(net, auth);
    TEST_ASSERT_TRUE(api.readCharge("5YJ30123456789ABC").ok);
    TEST_ASSERT_EQUAL(2, net.tokenCount);
    net.vehicleFirstStatus401s = 5;   // keeps refusing: it gives up after the one retry
    const FleetRead r = api.readCharge("5YJ30123456789ABC");
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(r.error.find("Sign in again") != std::string::npos);
}

void test_tesla_codes_come_out_in_plain_words() {
    FakePlatform p; FakeTesla net; MemoryStore store; store.creds = signedIn();
    FleetAuth auth(net, store, p); FleetApi api(net, auth);
    net.vehicleStatus = 402;
    TEST_ASSERT_TRUE(api.readCharge("V").error.find("billing") != std::string::npos);
    net.vehicleStatus = 429;
    TEST_ASSERT_TRUE(api.readCharge("V").error.find("too many") != std::string::npos);
    net.vehicleStatus = 503;
    TEST_ASSERT_TRUE(api.readCharge("V").error.find("503") != std::string::npos);
    net.online = false;
    TEST_ASSERT_TRUE(api.readCharge("V").error.find("reach Tesla") != std::string::npos);
}

// ---- the car service ---------------------------------------------------------------------------------------------------------

struct Rig {
    FakePlatform p; FakeTesla net; MemoryStore store; InlineExecutor exec; FakeIdentity id; testing::OpenSslCrypto crypto;
    FleetAuth auth; FleetApi api; FleetCar car;
    Rig() : auth(net, store, p), api(net, auth), car(api, auth, id, crypto, p, exec) {
        store.creds = signedIn();
        auth.reload();
        net.vehicleBody = kCharging;
        tesla::Bytes pub;
        crypto.generateKeyPair(id.key, pub);
    }
};

void test_the_car_is_only_paired_when_account_car_and_key_are_all_there() {
    Rig r;
    CarSnapshot s = r.car.snapshot();
    TEST_ASSERT_TRUE(s.paired);
    TEST_ASSERT_EQUAL_STRING("5YJ**********9ABC", s.vin.c_str());
    TEST_ASSERT_EQUAL(8, s.key.size());
    r.id.key.clear();
    TEST_ASSERT_FALSE(r.car.snapshot().paired);
    r.id.key = tesla::Bytes(32, 7);
    r.id.v = "";
    TEST_ASSERT_FALSE(r.car.snapshot().paired);
    TEST_ASSERT_FALSE(r.car.startRefresh().ok);   // and nothing is sent without them
}

void test_a_read_fills_the_snapshot_and_a_quiet_read_leaves_no_result_line() {
    Rig r;
    TEST_ASSERT_TRUE(r.car.startRefresh().ok);
    CarSnapshot s = r.car.snapshot();
    TEST_ASSERT_TRUE(s.hasReading);
    TEST_ASSERT_EQUAL(1, s.sleep);
    TEST_ASSERT_EQUAL(5, s.charge.chargingState);
    TEST_ASSERT_TRUE(s.hasResult);
    TEST_ASSERT_TRUE(s.resultOk);
    TEST_ASSERT_FALSE(s.busy);
    const uint32_t serial = s.resultSerial;
    r.car.startQuietRefresh();
    s = r.car.snapshot();
    TEST_ASSERT_TRUE(s.resultSerial == serial + 1);   // the commander can tell a new result happened
}

void test_a_sleeping_car_reads_as_asleep_and_stays_asleep() {
    Rig r;
    r.net.vehicleStatus = 408;
    r.car.startRefresh();
    const CarSnapshot s = r.car.snapshot();
    TEST_ASSERT_TRUE(s.hasReading);
    TEST_ASSERT_EQUAL(2, s.sleep);
    TEST_ASSERT_TRUE(s.resultOk);   // asleep is a real answer
    TEST_ASSERT_EQUAL(0, r.net.wakes);
}

void test_a_read_with_no_internet_fails_in_plain_words() {
    Rig r;
    r.net.online = false;
    r.car.startRefresh();
    const CarSnapshot s = r.car.snapshot();
    TEST_ASSERT_FALSE(s.resultOk);
    TEST_ASSERT_TRUE(s.resultLines[0].find("reach Tesla") != std::string::npos);
}

void test_wake_leaves_an_awake_car_alone() {
    Rig r;
    r.car.startWake();
    TEST_ASSERT_EQUAL(0, r.net.wakes);
    TEST_ASSERT_TRUE(r.car.snapshot().resultOk);
}

void test_wake_asks_once_then_looks_a_few_times() {
    Rig r;
    r.net.vehicleStatus = 408;
    r.car.startWake();
    TEST_ASSERT_EQUAL(1, r.net.wakes);
    TEST_ASSERT_EQUAL(FleetCar::kWakeLooks * FleetCar::kWakeLookEveryMs, r.exec.slept);
    TEST_ASSERT_FALSE(r.car.snapshot().resultOk);   // it never answered
}

void test_pairing_is_refused_here_and_bad_amps_never_leave_the_board() {
    Rig r;
    TEST_ASSERT_FALSE(r.car.startPair("5YJ30123456789ABC").ok);
    const size_t before = r.net.seen.size();
    TEST_ASSERT_FALSE(r.car.startSetAmps(5).ok);
    TEST_ASSERT_FALSE(r.car.startSetAmps(41).ok);
    TEST_ASSERT_EQUAL(before, r.net.seen.size());
}

void test_forget_deletes_the_account_link_and_the_readings() {
    Rig r;
    r.car.startRefresh();
    r.car.forget();
    TEST_ASSERT_FALSE(r.car.snapshot().paired);
    TEST_ASSERT_FALSE(r.car.snapshot().hasReading);
    TEST_ASSERT_EQUAL_STRING("", r.store.creds.refreshToken.c_str());
}

void test_the_watch_is_off_by_default_and_reads_rarely_when_on() {
    Rig r;
    r.car.loop();
    r.p.now += 3600UL * 1000;
    r.car.loop();
    TEST_ASSERT_EQUAL(0, r.net.seen.size());   // off: nothing leaves, nothing is billed
    r.car.setWatch(true);
    r.car.loop();   // schedules the first read a minute away
    TEST_ASSERT_EQUAL(0, r.net.seen.size());
    r.p.now += 61 * 1000;
    r.car.loop();
    const size_t first = r.net.seen.size();
    TEST_ASSERT_TRUE(first > 0);
    r.p.now += 5 * 60 * 1000;
    r.car.loop();
    TEST_ASSERT_EQUAL(first, r.net.seen.size());   // not again after five minutes
    r.car.setControlled(true);
    r.p.now += 3600UL * 1000;
    r.car.loop();
    TEST_ASSERT_EQUAL(first, r.net.seen.size());   // the commander reads while it is in command
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_base64_matches_the_standard_examples);
    RUN_TEST(test_base64_round_trips_every_byte_value);
    RUN_TEST(test_not_connected_gives_no_token_and_says_so);
    RUN_TEST(test_refresh_sends_only_the_client_id_and_the_refresh_token_and_keeps_the_new_one);
    RUN_TEST(test_a_token_is_reused_until_it_is_about_to_expire);
    RUN_TEST(test_a_refused_refresh_token_needs_a_new_sign_in_and_is_not_hammered);
    RUN_TEST(test_no_internet_is_offline_not_signed_out_and_retries_soon);
    RUN_TEST(test_region_picks_the_base_url_and_forget_deletes_the_credentials);
    RUN_TEST(test_charge_state_json_becomes_a_charge_state);
    RUN_TEST(test_every_charging_state_word_has_the_codes_the_controller_uses);
    RUN_TEST(test_reading_asks_for_the_charge_state_only_with_a_bearer_token_and_never_wakes);
    RUN_TEST(test_a_sleeping_car_is_an_answer_not_an_error);
    RUN_TEST(test_a_401_gets_one_new_token_and_one_retry);
    RUN_TEST(test_tesla_codes_come_out_in_plain_words);
    RUN_TEST(test_the_car_is_only_paired_when_account_car_and_key_are_all_there);
    RUN_TEST(test_a_read_fills_the_snapshot_and_a_quiet_read_leaves_no_result_line);
    RUN_TEST(test_a_sleeping_car_reads_as_asleep_and_stays_asleep);
    RUN_TEST(test_a_read_with_no_internet_fails_in_plain_words);
    RUN_TEST(test_wake_leaves_an_awake_car_alone);
    RUN_TEST(test_wake_asks_once_then_looks_a_few_times);
    RUN_TEST(test_pairing_is_refused_here_and_bad_amps_never_leave_the_board);
    RUN_TEST(test_forget_deletes_the_account_link_and_the_readings);
    RUN_TEST(test_the_watch_is_off_by_default_and_reads_rarely_when_on);
    return UNITY_END();
}
