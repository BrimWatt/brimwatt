// Host-side tests for the setup network's logic: what the form may contain, the Wi-Fi change that is tried and
// undone if the board cannot join, the scan list, and the setup API (secrets never come back out of it).
#include "../support/TestSupport.h"

#include "setup/SetupApi.h"
#include "setup/SetupLogic.h"

using namespace solar;
using namespace solar::setup;
using testing::contains;

namespace {

const char* kToken = "eyJhbGciOiJFUzI1NiJ9.eyJhdWQiOiIxMjM0NTY3ODkwIn0.c2lnbmF0dXJlLXNpZ25hdHVyZS1zaWduYXR1cmU";

class MemoryStore : public CredentialStore {
public:
    Credentials saved;
    TrialState trial;
    int saves = 0;
    Credentials load() override { return saved; }
    void save(const Credentials& credentials) override {
        saved = credentials;
        saves++;
    }
    TrialState loadTrial() override { return trial; }
    void saveTrial(const TrialState& state) override { trial = state; }
    void clear() override {
        saved = Credentials{};
        trial = TrialState{};
    }
};

class FakeHost : public SetupHost {
public:
    bool isActive = true;
    bool isScanning = false;
    std::vector<ScanEntry> found;
    uint32_t left = 600;
    int scans = 0, touches = 0, restarts = 0;
    bool active() const override { return isActive; }
    bool scanning() const override { return isScanning; }
    std::vector<ScanEntry> networks() const override { return found; }
    void startScan() override { scans++; }
    uint32_t secondsLeft() const override { return left; }
    void userActive() override { touches++; }
    void restartSoon() override { restarts++; }
};

Credentials everything() {
    Credentials c;
    c.wifiSsid = "Home";
    c.wifiPassword = "homepassword";
    c.enphaseToken = kToken;
    c.envoyIp = "192.168.1.112";
    c.otaPassword = "update-password";
    return c;
}

Form filledForm() {
    Form f;
    f.ssid = "Home";
    f.password = "homepassword";
    f.token = kToken;
    f.envoyIp = "192.168.1.112";
    f.updatePassword = "update-password";
    return f;
}

// ---------------- form validation ----------------

void test_a_complete_first_setup_is_accepted() {
    Validation v = validate(filledForm(), Credentials{});
    TEST_ASSERT_TRUE(v.ok());
    testing::check("Home", v.merged.wifiSsid);
    testing::check("homepassword", v.merged.wifiPassword);
    testing::check(kToken, v.merged.enphaseToken);
    testing::check("192.168.1.112", v.merged.envoyIp);
    testing::check("update-password", v.merged.otaPassword);
}

void test_an_empty_first_setup_says_what_is_missing() {
    testing::check("Choose your Wi-Fi network.", validate(Form{}, Credentials{}).error);

    Form f = filledForm();
    f.token.clear();
    testing::check("Paste the Enphase token.", validate(f, Credentials{}).error);

    f = filledForm();
    f.updatePassword.clear();
    testing::check("Choose an update password of 8 to 64 characters.", validate(f, Credentials{}).error);
}

void test_a_new_network_needs_its_password_unless_it_is_open() {
    Form f = filledForm();
    f.ssid = "Neighbour";
    f.password.clear();
    testing::check("Type the Wi-Fi password.", validate(f, everything()).error);

    f.open = true;
    Validation open = validate(f, everything());
    TEST_ASSERT_TRUE(open.ok());
    testing::check("Neighbour", open.merged.wifiSsid);
    testing::check("", open.merged.wifiPassword);
}

void test_wifi_password_length_and_characters() {
    Form f = filledForm();
    f.ssid = "Other";
    f.password = "short";
    testing::check("The Wi-Fi password must be 8 to 63 characters.", validate(f, everything()).error);
    f.password = std::string(64, 'a');
    TEST_ASSERT_FALSE(validate(f, everything()).ok());
    f.password = std::string(63, 'a');
    TEST_ASSERT_TRUE(validate(f, everything()).ok());
    f.password = "has\nline break";
    TEST_ASSERT_FALSE(validate(f, everything()).ok());
    f.password = "spaces are fine here";
    TEST_ASSERT_TRUE(validate(f, everything()).ok());
}

void test_the_same_network_with_nothing_typed_keeps_the_saved_password() {
    Form f;   // everything blank: only "keep"
    f.envoyIp = "192.168.1.112";
    Validation v = validate(f, everything());
    TEST_ASSERT_TRUE(v.ok());
    testing::check("Home", v.merged.wifiSsid);
    testing::check("homepassword", v.merged.wifiPassword);
    testing::check(kToken, v.merged.enphaseToken);
    testing::check("update-password", v.merged.otaPassword);
}

void test_the_wifi_name_is_checked() {
    Form f = filledForm();
    f.ssid = std::string(33, 'x');
    testing::check("The Wi-Fi name must be 1 to 32 characters.", validate(f, everything()).error);
    f.ssid = std::string(32, 'x');
    TEST_ASSERT_TRUE(validate(f, everything()).ok());
    f.ssid = "Home\r\n";   // a stray line break from pasting is dropped
    testing::check("Home", validate(f, everything()).merged.wifiSsid);
}

void test_a_pasted_token_is_cleaned() {
    testing::check(kToken, cleanToken(std::string("  \n") + kToken + "\n "));
    testing::check(kToken, cleanToken(std::string("\"") + kToken + "\""));
    testing::check(kToken, cleanToken(std::string("'") + kToken + "'"));
    testing::check(kToken, cleanToken(std::string("Bearer ") + kToken));
    testing::check(kToken, cleanToken(std::string("  bearer \"") + kToken + "\"  "));

    Form f = filledForm();
    f.token = std::string("Bearer ") + kToken + "\r\n";
    testing::check(kToken, validate(f, everything()).merged.enphaseToken);
}

void test_a_bad_token_is_refused() {
    const char* message = "That does not look like an Enphase token. Paste the whole token, with nothing around it.";
    Form f = filledForm();
    f.token = "tooshort";
    testing::check(message, validate(f, everything()).error);
    f.token = std::string("has a space inside ") + kToken;
    testing::check(message, validate(f, everything()).error);
    f.token = std::string(kToken) + "<script>";
    testing::check(message, validate(f, everything()).error);
    f.token = std::string(5000, 'a');
    testing::check(message, validate(f, everything()).error);
}

void test_the_envoy_address_is_optional_but_checked() {
    Form f = filledForm();
    f.envoyIp = "";
    testing::check("auto", validate(f, everything()).merged.envoyIp);   // saved as "auto" = find it as envoy.local

    f.envoyIp = " 10.0.0.5 ";
    testing::check("10.0.0.5", validate(f, everything()).merged.envoyIp);

    const char* message = "The Envoy address must look like 192.168.1.50, or be left empty to find it automatically.";
    for (const char* bad : {"300.1.1.1", "1.2.3", "1.2.3.4.5", "192.168.001.5", "0.0.0.0", "envoy.local", "1.2.3.x", "1..3.4", "-1.2.3.4"}) {
        f.envoyIp = bad;
        TEST_ASSERT_TRUE_MESSAGE(validate(f, everything()).error == message, bad);
    }
}

void test_the_update_password_rules() {
    const char* message = "The update password must be 8 to 64 characters, without spaces.";
    Form f = filledForm();
    f.updatePassword = "short";
    testing::check(message, validate(f, everything()).error);
    f.updatePassword = "has a space1";
    testing::check(message, validate(f, everything()).error);
    f.updatePassword = std::string(65, 'a');
    testing::check(message, validate(f, everything()).error);
    f.updatePassword = "long-enough-1";
    testing::check("long-enough-1", validate(f, everything()).merged.otaPassword);
}

void test_the_first_error_wins_and_nothing_is_merged_on_error() {
    Form f = filledForm();
    f.token = "bad";
    f.envoyIp = "nope";
    Validation v = validate(f, Credentials{});
    TEST_ASSERT_TRUE(contains(v.error, "Enphase token"));
}

// ---------------- compiled defaults ----------------

void test_saved_values_win_field_by_field_over_the_compiled_defaults() {
    Credentials defaults = everything();
    defaults.wifiSsid = "Compiled";
    defaults.wifiPassword = "compiledpass";
    defaults.otaPassword = "compiled-update";

    Credentials saved;
    saved.enphaseToken = "saved-token-saved-token-xx";
    Credentials now = effective(saved, defaults);
    testing::check("Compiled", now.wifiSsid);          // nothing saved: the compiled network
    testing::check("compiledpass", now.wifiPassword);
    testing::check("saved-token-saved-token-xx", now.enphaseToken);
    testing::check("compiled-update", now.otaPassword);
    testing::check("192.168.1.112", now.envoyIp);

    saved.wifiSsid = "Saved";
    saved.wifiPassword = "";   // an open network saved on purpose
    now = effective(saved, defaults);
    testing::check("Saved", now.wifiSsid);
    testing::check("", now.wifiPassword);              // not the compiled network's password
}

void test_auto_means_find_the_gateway_even_if_an_address_is_compiled_in() {
    Credentials defaults = everything();
    Credentials saved;
    saved.envoyIp = "auto";
    testing::check("", effective(saved, defaults).envoyIp);
    saved.envoyIp = "10.1.1.1";
    testing::check("10.1.1.1", effective(saved, defaults).envoyIp);
}

// ---------------- the scan list ----------------

void test_the_scan_list_is_tidied() {
    std::vector<ScanEntry> raw = {
        {"Weak", -80, true},
        {"", -40, true},                 // hidden network
        {"Home", -70, true},
        {"Strong", -45, false},
        {"Home", -52, true},             // the same name from a second access point: the stronger one wins
        {"Zebra", -60, true},
        {"Apple", -60, true},
    };
    std::vector<ScanEntry> tidy = tidyScan(raw);
    TEST_ASSERT_EQUAL(5, tidy.size());
    testing::check("Strong", tidy[0].ssid);
    TEST_ASSERT_FALSE(tidy[0].secure);
    testing::check("Home", tidy[1].ssid);
    TEST_ASSERT_EQUAL(-52, tidy[1].rssi);
    testing::check("Apple", tidy[2].ssid);   // equal signal: by name
    testing::check("Zebra", tidy[3].ssid);
    testing::check("Weak", tidy[4].ssid);
}

void test_an_empty_scan_is_fine() { TEST_ASSERT_EQUAL(0, tidyScan({}).size()); }

// ---------------- the Wi-Fi change that is tried and undone ----------------

void test_a_working_new_network_is_kept() {
    MemoryStore store;
    store.saved = everything();
    store.saved.wifiSsid = "New";
    store.trial.pending = true;
    store.trial.previousSsid = "Home";
    store.trial.previousPassword = "homepassword";

    TrialOutcome outcome = finishTrial(store, true, "");
    TEST_ASSERT_TRUE(outcome.wasTrial);
    TEST_ASSERT_TRUE(outcome.joined);
    testing::check("New", store.saved.wifiSsid);
    TEST_ASSERT_FALSE(store.trial.pending);
    testing::check("", store.trial.lastError);
}

void test_a_network_that_cannot_be_joined_is_replaced_by_the_old_one() {
    MemoryStore store;
    store.saved = everything();
    store.saved.wifiSsid = "New";
    store.saved.wifiPassword = "wrongpassword";
    store.saved.enphaseToken = "token-that-was-changed-too-xxxx";   // other settings are kept, only Wi-Fi is undone
    store.trial.pending = true;
    store.trial.previousSsid = "Home";
    store.trial.previousPassword = "homepassword";

    TrialOutcome outcome = finishTrial(store, false, "wrong password");
    TEST_ASSERT_TRUE(outcome.wasTrial);
    TEST_ASSERT_FALSE(outcome.joined);
    testing::check("Home", store.saved.wifiSsid);
    testing::check("homepassword", store.saved.wifiPassword);
    testing::check("token-that-was-changed-too-xxxx", store.saved.enphaseToken);
    TEST_ASSERT_FALSE(store.trial.pending);
    testing::check("Could not join \"New\": wrong password. The previous Wi-Fi was put back.", store.trial.lastError);
}

void test_a_failed_first_network_leaves_no_wifi_saved() {
    MemoryStore store;
    store.saved.wifiSsid = "New";
    store.saved.wifiPassword = "whatever1";
    store.trial.pending = true;   // there was no previous network

    finishTrial(store, false, "network not found");
    testing::check("", store.saved.wifiSsid);
    testing::check("Could not join \"New\": network not found. No Wi-Fi is saved.", store.trial.lastError);
}

void test_nothing_happens_when_no_trial_is_pending() {
    MemoryStore store;
    store.saved = everything();
    TrialOutcome outcome = finishTrial(store, false, "timed out");
    TEST_ASSERT_FALSE(outcome.wasTrial);
    testing::check("Home", store.saved.wifiSsid);
    TEST_ASSERT_EQUAL(0, store.saves);
}

// ---------------- the API ----------------

struct Rig {
    MemoryStore store;
    FakeHost host;
    SetupApi api{store, host, Credentials{}};
    ApiHandler::Params params(std::initializer_list<std::pair<const std::string, std::string>> p) { return ApiHandler::Params(p); }
};

void test_everything_is_refused_while_setup_is_off() {
    Rig r;
    r.host.isActive = false;
    for (const char* path : {"/api/setup/status", "/api/setup/networks"}) {
        HttpResponse response = r.api.handle("GET", path, {}, true);
        TEST_ASSERT_EQUAL(403, response.status);
        TEST_ASSERT_TRUE(contains(response.body, "Hold the BOOT button"));
    }
    TEST_ASSERT_EQUAL(403, r.api.handle("POST", "/api/setup/save", r.params({{"ssid", "Home"}}), true).status);
    TEST_ASSERT_EQUAL(0, r.store.saves);
    TEST_ASSERT_EQUAL(0, r.host.restarts);
}

void test_changing_requests_need_the_safe_header() {
    Rig r;
    HttpResponse response = r.api.handle("POST", "/api/setup/save", r.params({{"ssid", "Home"}}), false);
    TEST_ASSERT_EQUAL(403, response.status);
    TEST_ASSERT_TRUE(contains(response.body, "X-WattSync"));
    TEST_ASSERT_EQUAL(0, r.host.scans);
    TEST_ASSERT_EQUAL(403, r.api.handle("POST", "/api/setup/scan", {}, false).status);
    TEST_ASSERT_EQUAL(0, r.host.scans);
}

void test_status_never_contains_a_secret() {
    Rig r;
    r.store.saved = everything();
    r.store.trial.lastError = "Could not join \"X\": wrong password. The previous Wi-Fi was put back.";
    HttpResponse response = r.api.handle("GET", "/api/setup/status", {}, true);
    TEST_ASSERT_EQUAL(200, response.status);
    TEST_ASSERT_TRUE(contains(response.body, "\"ssid\":\"Home\""));
    TEST_ASSERT_TRUE(contains(response.body, "\"hasToken\":true"));
    TEST_ASSERT_TRUE(contains(response.body, "\"hasUpdatePassword\":true"));
    TEST_ASSERT_TRUE(contains(response.body, "\"envoyIp\":\"192.168.1.112\""));
    TEST_ASSERT_TRUE(contains(response.body, "wrong password"));
    TEST_ASSERT_TRUE(contains(response.body, "\"secondsLeft\":600"));
    TEST_ASSERT_FALSE(contains(response.body, "homepassword"));
    TEST_ASSERT_FALSE(contains(response.body, "update-password"));
    TEST_ASSERT_FALSE(contains(response.body, kToken));
    TEST_ASSERT_FALSE(contains(response.body, "eyJ"));
}

void test_the_network_list_is_served() {
    Rig r;
    r.host.found = {{"Home", -50, true}, {"Cafe", -70, false}};
    r.host.isScanning = true;
    HttpResponse response = r.api.handle("GET", "/api/setup/networks", {}, true);
    TEST_ASSERT_EQUAL(200, response.status);
    testing::check("application/json", response.contentType);
    TEST_ASSERT_TRUE(contains(response.body, "\"scanning\":true"));
    TEST_ASSERT_TRUE(contains(response.body, "{\"ssid\":\"Home\",\"rssi\":-50,\"secure\":true}"));
    TEST_ASSERT_TRUE(contains(response.body, "{\"ssid\":\"Cafe\",\"rssi\":-70,\"secure\":false}"));
}

void test_scan_again_starts_a_scan() {
    Rig r;
    TEST_ASSERT_EQUAL(200, r.api.handle("POST", "/api/setup/scan", {}, true).status);
    TEST_ASSERT_EQUAL(1, r.host.scans);
}

void test_using_the_page_keeps_it_open() {
    Rig r;
    r.api.handle("GET", "/api/setup/status", {}, true);
    r.api.handle("GET", "/api/setup/networks", {}, true);
    TEST_ASSERT_EQUAL(2, r.host.touches);
}

void test_saving_a_new_network_starts_a_trial_and_restarts() {
    Rig r;
    r.store.saved = everything();
    HttpResponse response = r.api.handle("POST", "/api/setup/save",
                                         r.params({{"ssid", "Moved"}, {"password", "newpassword1"}, {"envoyIp", "192.168.1.112"}}), true);
    TEST_ASSERT_EQUAL(200, response.status);
    TEST_ASSERT_TRUE(contains(response.body, "\"wifiChanged\":true"));
    testing::check("Moved", r.store.saved.wifiSsid);
    testing::check("newpassword1", r.store.saved.wifiPassword);
    testing::check(kToken, r.store.saved.enphaseToken);   // kept
    TEST_ASSERT_TRUE(r.store.trial.pending);
    testing::check("Home", r.store.trial.previousSsid);
    testing::check("homepassword", r.store.trial.previousPassword);
    TEST_ASSERT_EQUAL(1, r.host.restarts);
    TEST_ASSERT_FALSE(contains(response.body, "newpassword1"));
}

void test_changing_only_the_token_needs_no_wifi_trial() {
    Rig r;
    r.store.saved = everything();
    r.store.trial.lastError = "an old error";
    std::string newToken = "bmV3LXRva2VuLW5ldy10b2tlbi1uZXctdG9rZW4=";
    HttpResponse response = r.api.handle("POST", "/api/setup/save", r.params({{"token", newToken}, {"envoyIp", "192.168.1.112"}}), true);
    TEST_ASSERT_EQUAL(200, response.status);
    TEST_ASSERT_TRUE(contains(response.body, "\"wifiChanged\":false"));
    testing::check(newToken, r.store.saved.enphaseToken);
    testing::check("Home", r.store.saved.wifiSsid);
    TEST_ASSERT_FALSE(r.store.trial.pending);
    testing::check("", r.store.trial.lastError);   // a new save clears the old error
    TEST_ASSERT_EQUAL(1, r.host.restarts);
}

void test_a_bad_form_changes_nothing_and_does_not_restart() {
    Rig r;
    r.store.saved = everything();
    HttpResponse response = r.api.handle("POST", "/api/setup/save", r.params({{"token", "bad"}}), true);
    TEST_ASSERT_EQUAL(400, response.status);
    TEST_ASSERT_TRUE(contains(response.body, "Enphase token"));
    testing::check(kToken, r.store.saved.enphaseToken);
    TEST_ASSERT_EQUAL(0, r.store.saves);
    TEST_ASSERT_EQUAL(0, r.host.restarts);
}

void test_an_open_network_can_be_saved() {
    Rig r;
    r.store.saved = everything();
    r.api.handle("POST", "/api/setup/save", r.params({{"ssid", "Cafe"}, {"open", "1"}, {"envoyIp", ""}}), true);
    testing::check("Cafe", r.store.saved.wifiSsid);
    testing::check("", r.store.saved.wifiPassword);
    testing::check("auto", r.store.saved.envoyIp);
    TEST_ASSERT_TRUE(r.store.trial.pending);
}

void test_unknown_setup_paths_are_not_found_and_owned_by_the_setup_api() {
    Rig r;
    TEST_ASSERT_EQUAL(404, r.api.handle("GET", "/api/setup/nothing", {}, true).status);
    TEST_ASSERT_EQUAL(404, r.api.handle("GET", "/api/setup/save", {}, true).status);   // save is POST only
    TEST_ASSERT_TRUE(SetupApi::owns("/api/setup/status"));
    TEST_ASSERT_FALSE(SetupApi::owns("/api/telemetry"));
    TEST_ASSERT_FALSE(SetupApi::owns("/setup"));
}

void test_the_setup_network_password_is_a_valid_wpa2_password() {
    size_t length = std::string(kApPassword).size();
    TEST_ASSERT_TRUE(length >= 8 && length <= 63);
    TEST_ASSERT_TRUE(std::string(kApName).size() <= 32);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_complete_first_setup_is_accepted);
    RUN_TEST(test_an_empty_first_setup_says_what_is_missing);
    RUN_TEST(test_a_new_network_needs_its_password_unless_it_is_open);
    RUN_TEST(test_wifi_password_length_and_characters);
    RUN_TEST(test_the_same_network_with_nothing_typed_keeps_the_saved_password);
    RUN_TEST(test_the_wifi_name_is_checked);
    RUN_TEST(test_a_pasted_token_is_cleaned);
    RUN_TEST(test_a_bad_token_is_refused);
    RUN_TEST(test_the_envoy_address_is_optional_but_checked);
    RUN_TEST(test_the_update_password_rules);
    RUN_TEST(test_the_first_error_wins_and_nothing_is_merged_on_error);
    RUN_TEST(test_saved_values_win_field_by_field_over_the_compiled_defaults);
    RUN_TEST(test_auto_means_find_the_gateway_even_if_an_address_is_compiled_in);
    RUN_TEST(test_the_scan_list_is_tidied);
    RUN_TEST(test_an_empty_scan_is_fine);
    RUN_TEST(test_a_working_new_network_is_kept);
    RUN_TEST(test_a_network_that_cannot_be_joined_is_replaced_by_the_old_one);
    RUN_TEST(test_a_failed_first_network_leaves_no_wifi_saved);
    RUN_TEST(test_nothing_happens_when_no_trial_is_pending);
    RUN_TEST(test_everything_is_refused_while_setup_is_off);
    RUN_TEST(test_changing_requests_need_the_safe_header);
    RUN_TEST(test_status_never_contains_a_secret);
    RUN_TEST(test_the_network_list_is_served);
    RUN_TEST(test_scan_again_starts_a_scan);
    RUN_TEST(test_using_the_page_keeps_it_open);
    RUN_TEST(test_saving_a_new_network_starts_a_trial_and_restarts);
    RUN_TEST(test_changing_only_the_token_needs_no_wifi_trial);
    RUN_TEST(test_a_bad_form_changes_nothing_and_does_not_restart);
    RUN_TEST(test_an_open_network_can_be_saved);
    RUN_TEST(test_unknown_setup_paths_are_not_found_and_owned_by_the_setup_api);
    RUN_TEST(test_the_setup_network_password_is_a_valid_wpa2_password);
    return UNITY_END();
}
