// Host-side tests for the dashboard API: the telemetry JSON and the three POST endpoints.
#include "../support/TestSupport.h"
#include "notify/Notifier.h"

#include <ArduinoJson.h>

#include "daylight/DaylightSchedule.h"
#include "history/HistoryService.h"
#include "wallbox/ChargerRegistry.h"
#include "web/ApiHandler.h"
#include "wallbox/WallboxController.h"
#include "car/CarCommander.h"
#include "car/ChargeRouter.h"

using namespace solar;
using testing::FakeLink;
using testing::FakePlatform;

namespace {

class MemoryStore : public SettingsStore {
public:
    std::optional<Settings> load() override { return saved; }
    void save(const Settings& settings) override { saved = settings; }
    std::optional<Settings> saved;
};

class FakeSchedule : public DaylightSchedule {
public:
    bool open = true;
    bool isOpen(int64_t) const override { return open; }
    bool windowFor(int64_t epoch, int64_t& start, int64_t& end) const override {
        start = epoch - 3600;
        end = epoch + 3600;
        return withWindow;
    }
    bool withWindow = true;
    int startSeconds = -1, endSeconds = -1;
    void setMargins(int start, int end) override { startSeconds = start; endSeconds = end; }
    std::string describe(int64_t) const override { return "07:12 to 17:55"; }
};

class FakeDiagnostics : public DiagnosticsSource {
public:
    Diagnostics value;
    Diagnostics read() const override { return value; }
};

class FakeNotifySender : public NotifySender {
public:
    std::string answer;
    void sendAsync(const std::string&, const std::string&) override {}
    std::string sendNow(const std::string&, const std::string&) override { return answer; }
};

struct Rig {
    FakePlatform platform;
    FakeLink link;
    ChargerRegistry chargers;
    WallboxController wallbox{platform, chargers};
    SystemState state;
    FakeSchedule schedule;
    MemoryStore store;
    SolarController controller{platform, state, wallbox, schedule, store};
    HistoryService history{platform};
    ApiHandler api{platform, controller, wallbox, schedule};
    FakeNotifySender notifySender;
    Notifier notifier{platform, state, notifySender};

    Rig() {
        platform.epoch = 1760000000;
        controller.setHistory(&history);
        api.setHistory(&history);
        api.setNotifier(&notifier);
    }

    void connectCharger(ChargerStatus status) {
        chargers.add(1, "box", link, platform.nowMillis());
        chargers.find(1)->setStatus(status);
    }

    HttpResponse post(const std::string& path, const ApiHandler::Params& query = {}) {
        return api.handle("POST", path, query);
    }

    JsonDocument telemetry() {
        HttpResponse response = api.handle("GET", "/api/telemetry", {});
        TEST_ASSERT_EQUAL(200, response.status);
        testing::check("application/json", response.contentType);
        JsonDocument doc;
        TEST_ASSERT_FALSE(deserializeJson(doc, response.body));
        return doc;
    }
};

// ---- telemetry ----

void test_telemetry_before_any_reading_or_charger() {
    Rig r;
    JsonDocument t = r.telemetry();
    testing::check("ECO", t["currentMode"] | "");
    TEST_ASSERT_EQUAL(32, t["fastModeAmps"].as<int>());
    TEST_ASSERT_EQUAL(1000, t["gridAllowanceWatts"].as<int>());
    TEST_ASSERT_FALSE(t["chargerConnected"].as<bool>());
    testing::check("NONE", t["charger"]["status"] | "");
    TEST_ASSERT_FALSE(t["controller"]["hasReading"].as<bool>());
    testing::check("waiting for first reading", t["controller"]["reason"] | "");
    TEST_ASSERT_EQUAL(-1, t["controller"]["readingAgeSeconds"].as<int>());
    TEST_ASSERT_EQUAL(60, t["controller"]["pollSeconds"].as<int>());
    TEST_ASSERT_TRUE(t["enphaseOk"].as<bool>());
    TEST_ASSERT_TRUE(t["daylight"]["open"].as<bool>());
    testing::check("07:12 to 17:55", t["daylight"]["window"] | "");
}

void test_telemetry_shows_a_charging_car_and_the_latest_decision() {
    Rig r;
    r.connectCharger(ChargerStatus::Charging);
    r.chargers.find(1)->setTransactionId(7);
    r.chargers.find(1)->setAppliedAmps(12);

    r.platform.now += 60000;
    TEST_ASSERT_TRUE(r.controller.tick());
    r.controller.onMeterResult({true, {5000, -3000}, ""});
    r.platform.now += 10000;

    JsonDocument t = r.telemetry();
    TEST_ASSERT_TRUE(t["chargerConnected"].as<bool>());
    testing::check("CHARGING", t["charger"]["status"] | "");
    // The plentiful sun made the planner raise the limit, and with no measured current the draw is assumed
    // to equal that limit.
    double amps = t["charger"]["amps"].as<double>();
    TEST_ASSERT_TRUE(amps > 12);
    TEST_ASSERT_EQUAL(static_cast<int>(amps), t["charger"]["limitAmps"].as<int>());
    TEST_ASSERT_FALSE(t["charger"]["drawMeasured"].as<bool>());
    TEST_ASSERT_TRUE(t["controller"]["hasReading"].as<bool>());
    TEST_ASSERT_EQUAL(10, t["controller"]["readingAgeSeconds"].as<int>());
    TEST_ASSERT_FLOAT_WITHIN(0.01, 240, t["power"]["voltage"].as<double>());   // lets the dashboard turn amps into watts
    TEST_ASSERT_FLOAT_WITHIN(0.01, 5000, t["power"]["solarWatts"].as<double>());
    TEST_ASSERT_FLOAT_WITHIN(0.01, -3000, t["power"]["gridWatts"].as<double>());
    // chargerWatts = amps * 240 V; home = solar + grid - charger, never below zero.
    TEST_ASSERT_FLOAT_WITHIN(0.5, amps * 240, t["power"]["chargerWatts"].as<double>());
    double home = 5000 - 3000 - amps * 240;
    TEST_ASSERT_FLOAT_WITHIN(0.5, home < 0 ? 0 : home, t["power"]["homeWatts"].as<double>());
}

void test_status_names_follow_the_dashboard_convention() {
    Rig r;
    r.connectCharger(ChargerStatus::SuspendedEVSE);
    testing::check("SUSPENDED_EVSE", r.telemetry()["charger"]["status"] | "");
    r.chargers.find(1)->setStatus(ChargerStatus::Finishing);
    testing::check("FINISHING", r.telemetry()["charger"]["status"] | "");
}

void test_the_allowance_shown_by_the_controller_applies_to_eco_and_balanced_but_not_fast() {
    Rig r;
    TEST_ASSERT_EQUAL(1000, r.telemetry()["controller"]["gridAllowanceWatts"].as<int>());
    r.controller.setMode(ChargingMode::MaxSolar);
    TEST_ASSERT_EQUAL(1000, r.telemetry()["controller"]["gridAllowanceWatts"].as<int>());
    r.controller.setMode(ChargingMode::Fast);
    TEST_ASSERT_EQUAL(0, r.telemetry()["controller"]["gridAllowanceWatts"].as<int>());
    TEST_ASSERT_EQUAL(1000, r.telemetry()["gridAllowanceWatts"].as<int>());   // the setting itself is unchanged
}

void test_telemetry_reports_an_envoy_problem_and_the_fail_safe() {
    Rig r;
    r.connectCharger(ChargerStatus::Preparing);
    for (int i = 0; i < 4; i++) {
        r.platform.now += 60000;
        if (r.controller.tick()) r.controller.onMeterResult({false, {}, "timeout"});
    }
    JsonDocument t = r.telemetry();
    TEST_ASSERT_FALSE(t["enphaseOk"].as<bool>());
    testing::check("timeout", t["enphaseMessage"] | "");
    TEST_ASSERT_TRUE(t["controller"]["failSafe"].as<bool>());
}

void test_a_closed_window_is_reported() {
    Rig r;
    r.schedule.open = false;
    TEST_ASSERT_FALSE(r.telemetry()["daylight"]["open"].as<bool>());
    r.controller.setDaylightWindowEnabled(false);   // switched off means "always open"
    TEST_ASSERT_TRUE(r.telemetry()["daylight"]["open"].as<bool>());
    TEST_ASSERT_FALSE(r.telemetry()["daylight"]["enabled"].as<bool>());
}

// ---- POST /api/mode ----

void test_mode_can_be_changed() {
    Rig r;
    HttpResponse response = r.post("/api/mode", {{"mode", "MAX_SOLAR"}});
    TEST_ASSERT_EQUAL(200, response.status);
    testing::check("Mode updated", response.body);
    TEST_ASSERT_EQUAL(static_cast<int>(ChargingMode::MaxSolar), static_cast<int>(r.state.mode()));
    r.post("/api/mode", {{"mode", "eco"}});   // case does not matter
    TEST_ASSERT_EQUAL(static_cast<int>(ChargingMode::Eco), static_cast<int>(r.state.mode()));
    // The old name and a friendly spelling still work, and the mode is reported under its new name.
    TEST_ASSERT_EQUAL(200, r.post("/api/mode", {{"mode", "BALANCED"}}).status);
    TEST_ASSERT_EQUAL(static_cast<int>(ChargingMode::MaxSolar), static_cast<int>(r.state.mode()));
    r.post("/api/mode", {{"mode", "eco"}});
    TEST_ASSERT_EQUAL(200, r.post("/api/mode", {{"mode", "maxsolar"}}).status);
    TEST_ASSERT_EQUAL(static_cast<int>(ChargingMode::MaxSolar), static_cast<int>(r.state.mode()));
    testing::check("MAX_SOLAR", r.telemetry()["currentMode"] | "");
}

void test_fast_mode_with_a_current_starts_a_waiting_car() {
    Rig r;
    r.connectCharger(ChargerStatus::Preparing);
    HttpResponse response = r.post("/api/mode", {{"mode", "FAST"}, {"amps", "20"}});
    TEST_ASSERT_EQUAL(200, response.status);
    TEST_ASSERT_EQUAL(20, r.state.fastModeAmps());
    TEST_ASSERT_EQUAL(1, r.link.count("\"RemoteStartTransaction\""));
    TEST_ASSERT_TRUE(testing::contains(r.link.last(), "\"limit\":20"));
}

void test_a_bad_mode_or_current_changes_nothing() {
    Rig r;
    TEST_ASSERT_EQUAL(400, r.post("/api/mode").status);
    HttpResponse unknown = r.post("/api/mode", {{"mode", "TURBO"}});
    TEST_ASSERT_EQUAL(400, unknown.status);
    testing::check("Unknown mode: TURBO", unknown.body);

    for (const char* bad : {"5", "41", "abc", "12.5", "", "-6"}) {
        HttpResponse response = r.post("/api/mode", {{"mode", "FAST"}, {"amps", bad}});
        TEST_ASSERT_EQUAL(400, response.status);
        testing::check("Current must be a whole number from 6 to 40 A.", response.body);
    }
    TEST_ASSERT_EQUAL(static_cast<int>(ChargingMode::Eco), static_cast<int>(r.state.mode()));   // never half-applied
    TEST_ASSERT_EQUAL(32, r.state.fastModeAmps());
}

void test_a_current_is_ignored_unless_the_mode_is_fast() {
    Rig r;
    TEST_ASSERT_EQUAL(200, r.post("/api/mode", {{"mode", "ECO"}, {"amps", "99"}}).status);
    TEST_ASSERT_EQUAL(32, r.state.fastModeAmps());
}

// ---- POST /api/allowance and /api/daylight ----

void test_the_allowance_is_validated() {
    Rig r;
    TEST_ASSERT_EQUAL(200, r.post("/api/allowance", {{"watts", "500"}}).status);
    TEST_ASSERT_EQUAL(500, r.state.gridAllowanceWatts());
    TEST_ASSERT_EQUAL(200, r.post("/api/allowance", {{"watts", " 700 "}}).status);
    TEST_ASSERT_EQUAL(700, r.state.gridAllowanceWatts());
    TEST_ASSERT_EQUAL(200, r.post("/api/allowance", {{"watts", "0"}}).status);
    TEST_ASSERT_EQUAL(200, r.post("/api/allowance", {{"watts", "1000"}}).status);

    for (const char* bad : {"-1", "1001", "5000", "abc", "12.5", "1e3", "", "99999999999"}) {
        HttpResponse response = r.post("/api/allowance", {{"watts", bad}});
        TEST_ASSERT_EQUAL(400, response.status);
        testing::check("watts must be a whole number from 0 to 1000", response.body);
    }
    TEST_ASSERT_EQUAL(400, r.post("/api/allowance").status);
    TEST_ASSERT_EQUAL(1000, r.state.gridAllowanceWatts());
}

void test_the_daylight_window_can_be_switched() {
    Rig r;
    TEST_ASSERT_EQUAL(200, r.post("/api/daylight", {{"enabled", "false"}}).status);
    TEST_ASSERT_FALSE(r.state.daylightWindowEnabled());
    TEST_ASSERT_EQUAL(200, r.post("/api/daylight", {{"enabled", "true"}}).status);
    TEST_ASSERT_TRUE(r.state.daylightWindowEnabled());
    TEST_ASSERT_EQUAL(400, r.post("/api/daylight", {{"enabled", "yes"}}).status);
    TEST_ASSERT_EQUAL(400, r.post("/api/daylight").status);
}

void test_the_daylight_margins_are_validated_applied_and_saved() {
    Rig r;
    TEST_ASSERT_EQUAL(30, r.telemetry()["daylight"]["startMinutes"].as<int>());   // defaults: 30 min after sunrise,
    TEST_ASSERT_EQUAL(60, r.telemetry()["daylight"]["endMinutes"].as<int>());     // 60 min before sunset

    TEST_ASSERT_EQUAL(200, r.post("/api/daylight", {{"startMin", "15"}}).status);   // one margin alone keeps the other
    TEST_ASSERT_EQUAL(15, r.state.daylightStartMinutes());
    TEST_ASSERT_EQUAL(60, r.state.daylightEndMinutes());
    TEST_ASSERT_EQUAL(900, r.schedule.startSeconds);
    TEST_ASSERT_EQUAL(3600, r.schedule.endSeconds);
    TEST_ASSERT_EQUAL(200, r.post("/api/daylight", {{"startMin", "0"}, {"endMin", "180"}}).status);
    TEST_ASSERT_EQUAL(180, r.store.saved->daylightEndMinutes);
    TEST_ASSERT_EQUAL(0, r.store.saved->daylightStartMinutes);

    for (const char* bad : {"-1", "181", "abc", "1.5", ""}) {
        TEST_ASSERT_EQUAL(400, r.post("/api/daylight", {{"startMin", bad}}).status);
        TEST_ASSERT_EQUAL(400, r.post("/api/daylight", {{"endMin", bad}}).status);
    }
    // A rejected request changes nothing, even when another value in it was fine.
    TEST_ASSERT_EQUAL(400, r.post("/api/daylight", {{"enabled", "false"}, {"startMin", "999"}}).status);
    TEST_ASSERT_TRUE(r.state.daylightWindowEnabled());
    TEST_ASSERT_EQUAL(0, r.state.daylightStartMinutes());
    // Switching the window on or off leaves the margins alone.
    TEST_ASSERT_EQUAL(200, r.post("/api/daylight", {{"enabled", "false"}}).status);
    TEST_ASSERT_EQUAL(180, r.state.daylightEndMinutes());
}

void test_the_maximum_current_limits_every_mode() {
    Rig r;
    TEST_ASSERT_EQUAL(40, r.telemetry()["maxAmps"].as<int>());
    r.post("/api/mode", {{"mode", "FAST"}, {"amps", "30"}});
    TEST_ASSERT_EQUAL(200, r.post("/api/maxamps", {{"amps", "16"}}).status);
    TEST_ASSERT_EQUAL(16, r.state.maxAmps());
    TEST_ASSERT_EQUAL(16, r.state.fastModeAmps());                         // Custom was above it, so it is lowered
    TEST_ASSERT_EQUAL(16, r.store.saved->maxAmps);
    HttpResponse tooMuch = r.post("/api/mode", {{"mode", "FAST"}, {"amps", "20"}});
    TEST_ASSERT_EQUAL(400, tooMuch.status);
    testing::check("Current must be a whole number from 6 to 16 A.", tooMuch.body);
    TEST_ASSERT_EQUAL(200, r.post("/api/mode", {{"mode", "FAST"}, {"amps", "16"}}).status);
    for (const char* bad : {"5", "41", "abc", "", "12.5"}) TEST_ASSERT_EQUAL(400, r.post("/api/maxamps", {{"amps", bad}}).status);
    TEST_ASSERT_EQUAL(16, r.state.maxAmps());                              // rejected values change nothing
}

void test_the_departure_goal_endpoint_and_telemetry() {
    Rig r;
    TEST_ASSERT_EQUAL(0, r.telemetry()["goal"]["kwh"].as<int>());
    TEST_ASSERT_EQUAL(200, r.post("/api/goal", {{"kwh", "20"}, {"byMinutes", "435"}}).status);
    TEST_ASSERT_EQUAL(20, r.telemetry()["goal"]["kwh"].as<int>());
    TEST_ASSERT_EQUAL(435, r.telemetry()["goal"]["byMinutes"].as<int>());
    TEST_ASSERT_EQUAL(20, r.store.saved->goalKwh);
    TEST_ASSERT_EQUAL(200, r.post("/api/goal", {{"kwh", "0"}}).status);               // off, time kept
    TEST_ASSERT_EQUAL(435, r.state.goalMinutes());
    for (const char* bad : {"-1", "101", "abc", "", "1.5"}) TEST_ASSERT_EQUAL(400, r.post("/api/goal", {{"kwh", bad}}).status);
    TEST_ASSERT_EQUAL(400, r.post("/api/goal", {{"kwh", "10"}, {"byMinutes", "1440"}}).status);
    TEST_ASSERT_EQUAL(400, r.post("/api/goal", {}).status);
}

void test_notifications_are_set_tested_masked_and_saved() {
    Rig r;
    testing::check("", r.telemetry()["notifyTo"] | "x");
    TEST_ASSERT_EQUAL(200, r.post("/api/notify", {{"url", "https://hooks.example.com/services/SECRET"}}).status);
    testing::check("https://hooks.example.com/…", r.telemetry()["notifyTo"] | "");          // the secret part is never shown back
    testing::check("https://hooks.example.com/services/SECRET", r.store.saved->notifyUrl);
    TEST_ASSERT_EQUAL(200, r.post("/api/notify/test", {}).status);
    r.notifySender.answer = "the server answered 500";
    HttpResponse failed = r.post("/api/notify/test", {});
    TEST_ASSERT_EQUAL(502, failed.status);
    testing::check("Could not send: the server answered 500", failed.body);
    for (const char* bad : {"ftp://x.com", "nonsense", "https://"}) TEST_ASSERT_EQUAL(400, r.post("/api/notify", {{"url", bad}}).status);
    TEST_ASSERT_EQUAL(400, r.post("/api/notify", {}).status);
    TEST_ASSERT_EQUAL(200, r.post("/api/notify", {{"url", ""}}).status);                      // empty turns it off
    testing::check("", r.telemetry()["notifyTo"] | "x");
}

void test_the_pause_endpoint_and_telemetry() {
    Rig r;
    TEST_ASSERT_FALSE(r.telemetry()["paused"].as<bool>());

    HttpResponse paused = r.post("/api/pause", {{"enabled", "true"}});
    TEST_ASSERT_EQUAL(200, paused.status);
    testing::check("Charging paused", paused.body);
    TEST_ASSERT_TRUE(r.state.paused());
    TEST_ASSERT_TRUE(r.telemetry()["paused"].as<bool>());
    testing::check("ECO", r.telemetry()["currentMode"] | "");   // the mode is untouched

    HttpResponse resumed = r.post("/api/pause", {{"enabled", "false"}});
    TEST_ASSERT_EQUAL(200, resumed.status);
    testing::check("Charging resumed", resumed.body);
    TEST_ASSERT_FALSE(r.telemetry()["paused"].as<bool>());

    TEST_ASSERT_EQUAL(400, r.post("/api/pause", {{"enabled", "maybe"}}).status);
    TEST_ASSERT_EQUAL(400, r.post("/api/pause").status);
}

void test_pausing_through_the_api_stops_a_running_charge_and_is_saved() {
    Rig r;
    r.connectCharger(ChargerStatus::Charging);
    r.chargers.find(1)->setTransactionId(3);
    r.chargers.find(1)->setAppliedAmps(12);
    r.post("/api/pause", {{"enabled", "true"}});
    TEST_ASSERT_EQUAL(1, r.link.count("\"RemoteStopTransaction\""));
    TEST_ASSERT_TRUE(r.store.saved->paused);
}

void test_changes_through_the_api_are_saved() {
    Rig r;
    r.post("/api/mode", {{"mode", "MAX_SOLAR"}});
    r.post("/api/allowance", {{"watts", "300"}});
    r.post("/api/daylight", {{"enabled", "false"}});
    TEST_ASSERT_TRUE(r.store.saved.has_value());
    TEST_ASSERT_EQUAL(static_cast<int>(ChargingMode::MaxSolar), static_cast<int>(r.store.saved->mode));
    TEST_ASSERT_EQUAL(300, r.store.saved->gridAllowanceWatts);
    TEST_ASSERT_FALSE(r.store.saved->daylightWindowEnabled);
}

void test_the_last_action_is_reported_in_plain_words_with_its_time() {
    Rig r;
    JsonDocument before = r.telemetry();
    testing::check("None yet", before["charger"]["lastCommand"] | "");
    TEST_ASSERT_EQUAL(0, before["charger"]["lastCommandAt"].as<int>());

    r.connectCharger(ChargerStatus::Preparing);
    r.platform.epoch = 1760000000;
    r.post("/api/mode", {{"mode", "FAST"}, {"amps", "10"}});
    JsonDocument after = r.telemetry();
    testing::check("Started charging at 10 A", after["charger"]["lastCommand"] | "");
    TEST_ASSERT_EQUAL(1760000000, after["charger"]["lastCommandAt"].as<int64_t>());
}

void test_the_history_endpoint_returns_todays_samples_totals_and_events() {
    Rig r;
    r.history.recordSample(3000, -500, 2000, ChargingMode::Eco, false);
    r.platform.epoch += 60;
    r.history.recordSample(3000, -500, 2000, ChargingMode::Eco, false);
    r.history.addEvent("Car plugged in");

    HttpResponse response = r.api.handle("GET", "/api/history", {});
    TEST_ASSERT_EQUAL(200, response.status);
    testing::check("application/json", response.contentType);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, response.body));
    TEST_ASSERT_EQUAL(1760000060, doc["now"].as<int64_t>());
    TEST_ASSERT_TRUE(doc["dayStart"].as<int64_t>() <= 1760000060);
    TEST_ASSERT_EQUAL(1, doc["samples"].size());                       // both fall in one 5-minute bucket
    TEST_ASSERT_EQUAL(3000, doc["samples"][0][1].as<int>());           // solar
    TEST_ASSERT_EQUAL(2000, doc["samples"][0][4].as<int>());           // car
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.05, doc["totals"]["solarKwh"].as<double>());   // 3 kW for one minute
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.03, doc["totals"]["carKwh"].as<double>());
    TEST_ASSERT_TRUE(doc["events"].isNull());   // the activity log has its own paged endpoint now
    TEST_ASSERT_FALSE(doc["daylight"].isNull());
}

void test_the_history_endpoint_works_with_nothing_recorded_and_no_window() {
    Rig r;
    r.schedule.withWindow = false;
    HttpResponse response = r.api.handle("GET", "/api/history", {});
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, response.body));
    TEST_ASSERT_EQUAL(0, doc["samples"].size());
    TEST_ASSERT_TRUE(doc["daylight"].isNull());
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0, doc["totals"]["carKwh"].as<double>());
}

void test_the_price_endpoint_and_telemetry() {
    Rig r;
    TEST_ASSERT_EQUAL(30, r.telemetry()["rateCents"].as<int>());
    HttpResponse ok = r.post("/api/rate", {{"cents", "34"}});
    TEST_ASSERT_EQUAL(200, ok.status);
    testing::check("Price updated", ok.body);
    TEST_ASSERT_EQUAL(34, r.telemetry()["rateCents"].as<int>());
    for (const char* bad : {"-1", "201", "abc", "12.5", ""}) {
        HttpResponse response = r.post("/api/rate", {{"cents", bad}});
        TEST_ASSERT_EQUAL(400, response.status);
        testing::check("cents and peakCents must be whole numbers from 0 to 200", response.body);
    }
    TEST_ASSERT_EQUAL(400, r.post("/api/rate").status);                    // nothing given
    TEST_ASSERT_EQUAL(34, r.telemetry()["rateCents"].as<int>());
    TEST_ASSERT_TRUE(r.store.saved->rateCents == 34);
}

void test_a_past_day_comes_from_the_coarser_records() {
    Rig r;
    int64_t start = r.platform.epoch;
    for (int i = 0; i < 3 * 1440; i++) {            // three days, one reading a minute
        r.platform.epoch += 60;
        r.history.recordSample(2000, 0, 0, ChargingMode::Eco, false);
    }
    HttpResponse response = r.api.handle("GET", "/api/history", {{"daysAgo", "2"}});
    TEST_ASSERT_EQUAL(200, response.status);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, response.body));
    TEST_ASSERT_EQUAL(2, doc["daysAgo"].as<int>());
    TEST_ASSERT_TRUE(doc["samples"].size() > 100);   // a day in 5-minute steps (about 288 points)
    TEST_ASSERT_TRUE(doc["dayStart"].as<int64_t>() < start + 86400 && doc["dayStart"].as<int64_t>() > start - 86400);
}

void test_a_bad_daysago_is_refused() {
    Rig r;
    for (const char* bad : {"-1", "365", "abc", "1.5", ""}) {
        TEST_ASSERT_EQUAL(400, r.api.handle("GET", "/api/history", {{"daysAgo", bad}}).status);
    }
    TEST_ASSERT_EQUAL(200, r.api.handle("GET", "/api/history", {{"daysAgo", "364"}}).status);
}

void test_the_peak_price_and_its_hours_are_validated_applied_and_saved() {
    Rig r;
    TEST_ASSERT_EQUAL(0, r.telemetry()["peakRateCents"].as<int>());                    // off until set
    TEST_ASSERT_EQUAL(200, r.post("/api/rate", {{"peakCents", "52"}, {"peakStart", "17"}, {"peakEnd", "21"}}).status);
    TEST_ASSERT_EQUAL(52, r.telemetry()["peakRateCents"].as<int>());
    TEST_ASSERT_EQUAL(17, r.telemetry()["peakStartHour"].as<int>());
    TEST_ASSERT_EQUAL(21, r.telemetry()["peakEndHour"].as<int>());
    TEST_ASSERT_EQUAL(30, r.telemetry()["rateCents"].as<int>());                        // the normal price was left alone
    TEST_ASSERT_EQUAL(52, r.store.saved->peakRateCents);
    TEST_ASSERT_EQUAL(200, r.post("/api/rate", {{"cents", "28"}}).status);              // a part not given keeps its value
    TEST_ASSERT_EQUAL(52, r.state.peakRateCents());
    TEST_ASSERT_EQUAL(17, r.state.peakStartHour());
    std::vector<ApiHandler::Params> badRequests;
    badRequests.push_back({{"peakCents", "201"}});
    badRequests.push_back({{"peakStart", "24"}});
    badRequests.push_back({{"peakEnd", "0"}});
    badRequests.push_back({{"peakEnd", "25"}});
    badRequests.push_back({{"peakStart", "9"}, {"peakEnd", "9"}});          // the same hour: an empty peak
    badRequests.push_back({{"peakCents", "x"}});
    for (const auto& bad : badRequests) TEST_ASSERT_EQUAL(400, r.api.handle("POST", "/api/rate", bad).status);
    TEST_ASSERT_EQUAL(52, r.state.peakRateCents());                                      // rejected values change nothing
    TEST_ASSERT_EQUAL(200, r.post("/api/rate", {{"peakCents", "0"}}).status);           // 0 turns the peak price off
    TEST_ASSERT_EQUAL(0, r.state.peakRateCents());
}

void test_the_days_endpoint_lists_daily_totals() {
    Rig r;
    for (int i = 0; i < 2 * 1440; i++) {
        r.platform.epoch += 60;
        r.history.recordSample(3000, -500, 2000, ChargingMode::Eco, false);
    }
    HttpResponse response = r.api.handle("GET", "/api/days", {{"n", "7"}});
    TEST_ASSERT_EQUAL(200, response.status);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, response.body));
    TEST_ASSERT_TRUE(doc["days"].size() >= 2);
    JsonArray row = doc["days"][0];
    TEST_ASSERT_EQUAL(7, row.size());                // day, solar, car, car from sun, import, export, saved
    TEST_ASSERT_TRUE(row[6].as<double>() > 1);       // about 30 cents a kWh on the sun's share of 2 kW for a day
    TEST_ASSERT_TRUE(row[1].as<double>() > 10);      // solar kWh for a nearly full day of 3 kW
    TEST_ASSERT_EQUAL(400, r.api.handle("GET", "/api/days", {{"n", "0"}}).status);
    TEST_ASSERT_EQUAL(400, r.api.handle("GET", "/api/days", {{"n", "366"}}).status);
}

void test_the_csv_export_has_a_header_and_one_line_per_day() {
    Rig r;
    for (int i = 0; i < 2 * 1440; i++) {
        r.platform.epoch += 60;
        r.history.recordSample(3000, 0, 1000, ChargingMode::Eco, false);
    }
    HttpResponse response = r.api.handle("GET", "/api/export.csv", {});
    TEST_ASSERT_EQUAL(200, response.status);
    testing::check("text/csv", response.contentType);
    testing::check("wattsync-days.csv", response.downloadName);
    TEST_ASSERT_TRUE(testing::contains(response.body, "date,solar_kwh,car_kwh"));
    TEST_ASSERT_TRUE(testing::contains(response.body, "2025-10-"));   // dates are real calendar dates (epoch 1760000000 is Oct 2025)
}

void test_the_sessions_endpoint_lists_finished_and_running_sessions() {
    Rig r;
    r.history.recordSample(2000, 0, 0, ChargingMode::Eco, false);
    r.history.sessionStarted(1000);
    for (int i = 0; i < 60; i++) { r.platform.epoch += 60; r.history.recordSample(3000, -500, 2400, ChargingMode::Eco, false); }
    r.history.sessionStopped(3350, "Remote");                         // 2350 Wh by the wallbox's meter
    r.history.sessionStarted(5000);
    for (int i = 0; i < 10; i++) { r.platform.epoch += 60; r.history.recordSample(3000, -500, 2400, ChargingMode::Eco, false); }

    HttpResponse response = r.api.handle("GET", "/api/sessions", {{"n", "5"}});
    TEST_ASSERT_EQUAL(200, response.status);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, response.body));
    TEST_ASSERT_EQUAL(1, doc["sessions"].size());
    TEST_ASSERT_FLOAT_WITHIN(0.01, 2.35, doc["sessions"][0]["kwh"].as<double>());
    TEST_ASSERT_EQUAL(100, doc["sessions"][0]["sunPct"].as<int>());
    TEST_ASSERT_FLOAT_WITHIN(0.2, 9.8, doc["sessions"][0]["avgAmps"].as<double>());   // 2.35 kWh in an hour is about 9.8 A at 240 V
    testing::check("Remote", doc["sessions"][0]["reason"] | "");
    TEST_ASSERT_FALSE(doc["open"].isNull());
    TEST_ASSERT_TRUE(doc["open"]["kwh"].as<double>() > 0.2);
    TEST_ASSERT_EQUAL(0, doc["open"]["end"].as<int>());
    TEST_ASSERT_EQUAL(400, r.api.handle("GET", "/api/sessions", {{"n", "0"}}).status);
    TEST_ASSERT_EQUAL(400, r.api.handle("GET", "/api/sessions", {{"n", "101"}}).status);
}

void test_changing_requests_without_the_safe_header_are_refused() {
    Rig r;
    HttpResponse blocked = r.api.handle("POST", "/api/pause", {{"enabled", "true"}}, false);
    TEST_ASSERT_EQUAL(403, blocked.status);
    TEST_ASSERT_FALSE(r.state.paused());                                   // nothing changed
    TEST_ASSERT_EQUAL(403, r.api.handle("POST", "/api/mode", {{"mode", "FAST"}, {"amps", "40"}}, false).status);
    TEST_ASSERT_EQUAL(static_cast<int>(ChargingMode::Eco), static_cast<int>(r.state.mode()));
    TEST_ASSERT_EQUAL(200, r.api.handle("GET", "/api/telemetry", {}, false).status);   // reading needs no header
    TEST_ASSERT_EQUAL(200, r.api.handle("POST", "/api/pause", {{"enabled", "true"}}, true).status);
    TEST_ASSERT_TRUE(r.state.paused());
}

void test_events_come_one_page_at_a_time_newest_first_with_the_total() {
    Rig r;
    for (int i = 1; i <= 12; i++) { r.platform.epoch += 300; r.history.addEvent("Event " + std::to_string(i)); }

    JsonDocument first, second, last;
    TEST_ASSERT_FALSE(deserializeJson(first, r.api.handle("GET", "/api/events", {{"n", "5"}}).body));
    TEST_ASSERT_EQUAL(12, first["total"].as<int>());
    TEST_ASSERT_EQUAL(5, first["events"].size());
    testing::check("Event 12", first["events"][0]["text"] | "");
    TEST_ASSERT_FALSE(deserializeJson(second, r.api.handle("GET", "/api/events", {{"n", "5"}, {"offset", "5"}}).body));
    TEST_ASSERT_EQUAL(5, second["events"].size());
    testing::check("Event 7", second["events"][0]["text"] | "");
    TEST_ASSERT_FALSE(deserializeJson(last, r.api.handle("GET", "/api/events", {{"n", "5"}, {"offset", "10"}}).body));
    TEST_ASSERT_EQUAL(2, last["events"].size());
    testing::check("Event 1", last["events"][1]["text"] | "");
    TEST_ASSERT_EQUAL(5, [&] { JsonDocument d; deserializeJson(d, r.api.handle("GET", "/api/events", {}).body); return (int)d["events"].size(); }());   // five by default

    TEST_ASSERT_EQUAL(400, r.api.handle("GET", "/api/events", {{"n", "0"}}).status);
    TEST_ASSERT_EQUAL(400, r.api.handle("GET", "/api/events", {{"n", "21"}}).status);
    TEST_ASSERT_EQUAL(400, r.api.handle("GET", "/api/events", {{"offset", "-1"}}).status);
    TEST_ASSERT_EQUAL(400, r.api.handle("GET", "/api/events", {{"offset", "1001"}}).status);
    TEST_ASSERT_EQUAL(400, r.api.handle("GET", "/api/events", {{"offset", "abc"}}).status);
}

void test_diagnostics_are_404_without_a_source_and_list_only_what_the_platform_knows() {
    Rig r;
    TEST_ASSERT_EQUAL(404, r.api.handle("GET", "/api/diagnostics", {}).status);

    FakeDiagnostics fake;
    fake.value.platform = "ESP32-S3";
    fake.value.uptimeSeconds = 3700;
    fake.value.lastRestart = "power on (normal start)";
    fake.value.restarts = 7;
    fake.value.crashes = 2;
    fake.value.lastCrashReason = "CRASH (task watchdog)";
    fake.value.lastCrashAt = 1759990000;
    fake.value.freeHeapKb = 150; fake.value.minFreeHeapKb = 120; fake.value.largestBlockKb = 90;
    fake.value.hasWifi = true; fake.value.wifiRssi = -61;
    r.api.setDiagnostics(&fake);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, r.api.handle("GET", "/api/diagnostics", {}).body));
    testing::check("ESP32-S3", doc["platform"] | "");
    TEST_ASSERT_EQUAL(3700, doc["uptimeSeconds"].as<int>());
    TEST_ASSERT_EQUAL(7, doc["restarts"].as<int>());
    TEST_ASSERT_EQUAL(2, doc["crashes"].as<int>());
    testing::check("CRASH (task watchdog)", doc["lastCrash"]["reason"] | "");
    TEST_ASSERT_EQUAL(120, doc["memory"]["minFreeKb"].as<int>());
    TEST_ASSERT_EQUAL(-61, doc["wifiRssi"].as<int>());
    TEST_ASSERT_TRUE(doc["firmwareBuilt"].isNull());   // not supplied, so not listed

    fake.value = Diagnostics();                         // a platform that counts nothing
    fake.value.platform = "Raspberry Pi";
    JsonDocument bare;
    TEST_ASSERT_FALSE(deserializeJson(bare, r.api.handle("GET", "/api/diagnostics", {}).body));
    TEST_ASSERT_TRUE(bare["restarts"].isNull());
    TEST_ASSERT_TRUE(bare["lastCrash"].isNull());
    TEST_ASSERT_TRUE(bare["memory"].isNull());
    TEST_ASSERT_TRUE(bare["wifiRssi"].isNull());
}

void test_sessions_can_be_paged_with_an_offset() {
    Rig r;
    for (int i = 1; i <= 7; i++) {
        r.platform.epoch += 600; r.history.sessionStarted(i * 1000);
        for (int m = 0; m < 3; m++) { r.platform.epoch += 60; r.history.recordSample(3000, -500, 2400, ChargingMode::Eco, false); }
        r.history.sessionStopped(i * 1000 + 100, "Remote");
    }
    JsonDocument page2;
    TEST_ASSERT_FALSE(deserializeJson(page2, r.api.handle("GET", "/api/sessions", {{"n", "5"}, {"offset", "5"}}).body));
    TEST_ASSERT_EQUAL(7, page2["total"].as<int>());
    TEST_ASSERT_EQUAL(2, page2["sessions"].size());
    TEST_ASSERT_TRUE(page2["sessions"][0]["start"].as<int64_t>() > page2["sessions"][1]["start"].as<int64_t>());   // still newest first
    TEST_ASSERT_EQUAL(400, r.api.handle("GET", "/api/sessions", {{"offset", "-3"}}).status);
}

void test_history_carries_todays_day_number_so_times_can_be_labelled() {
    Rig r;
    HttpResponse response = r.api.handle("GET", "/api/history", {});
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, response.body));
    TEST_ASSERT_EQUAL(r.history.localDay(r.platform.epoch), doc["today"].as<int64_t>());
}

// ---- routing ----

void test_unknown_paths_and_wrong_methods_are_not_found() {
    Rig r;
    TEST_ASSERT_EQUAL(404, r.api.handle("GET", "/nope", {}).status);
    TEST_ASSERT_EQUAL(404, r.api.handle("GET", "/api/mode", {}).status);
    TEST_ASSERT_EQUAL(404, r.api.handle("POST", "/api/telemetry", {}).status);
}

}  // namespace

void setUp() {}
void tearDown() {}

// ---- the car (Tesla) endpoints ---------------------------------------------------------------------------------------------------

class FakeCar : public CarControl {
public:
    CarSnapshot snap;
    std::string lastCall;
    std::string lastVin;
    int lastAmps = 0;
    bool refuseBusy = false;
    CarSnapshot snapshot() const override { return snap; }
    CarResult done(const std::string& what) {
        lastCall = what;
        CarResult r;
        if (refuseBusy) { r.ok = false; r.busy = true; r.message = "A car job is already running."; }
        return r;
    }
    CarResult startPair(const std::string& vin) override { lastVin = vin; return done("pair"); }
    CarResult startRefresh() override { return done("refresh"); }
    CarResult startStart() override { return done("start"); }
    CarResult startStop() override { return done("stop"); }
    CarResult startSetAmps(int amps) override { lastAmps = amps; return done("amps"); }
    void cancel() override { lastCall = "cancel"; }
    void setWatch(bool on) override { lastCall = on ? "watch-on" : "watch-off"; snap.watch = on; }
    void forget() override { lastCall = "forget"; snap.paired = false; }
};

void test_car_endpoints_say_not_supported_without_a_car_link() {
    Rig r;
    TEST_ASSERT_FALSE(r.telemetry()["car"]["supported"].as<bool>());
    HttpResponse reply = r.post("/api/car/pair", {{"vin", "5YJYGDEE2MF284667"}});
    TEST_ASSERT_EQUAL(400, reply.status);
    TEST_ASSERT_TRUE(reply.body.find("ESP32") != std::string::npos);
    TEST_ASSERT_EQUAL(400, r.post("/api/car/test", {{"action", "stop"}}).status);
}

void test_telemetry_shows_the_car_without_the_vin_or_key() {
    Rig r;
    FakeCar car;
    r.api.setCar(&car);
    car.snap.paired = true;
    car.snap.vin = "5YJ**********4667";
    car.snap.key = "9b4593c7";
    car.snap.hasReading = true;
    car.snap.readingAgeSeconds = 42;
    car.snap.sleep = 1;
    car.snap.charge.valid = true;
    car.snap.charge.chargingState = 5;
    car.snap.charge.hasBatteryLevel = true; car.snap.charge.batteryLevel = 46;
    car.snap.charge.hasChargeLimit = true; car.snap.charge.chargeLimit = 80;
    car.snap.charge.hasActualCurrent = true; car.snap.charge.actualCurrent = 18;
    car.snap.charge.hasCurrentRequest = true; car.snap.charge.currentRequest = 18;
    car.snap.charge.hasVoltage = true; car.snap.charge.voltage = 239;
    car.snap.restoreAmps = 18;
    JsonDocument t = r.telemetry();
    JsonObject c = t["car"];
    TEST_ASSERT_TRUE(c["supported"].as<bool>());
    TEST_ASSERT_TRUE(c["paired"].as<bool>());
    testing::check("5YJ**********4667", c["vin"] | "");
    testing::check("charging", c["reading"]["charging"] | "");
    TEST_ASSERT_EQUAL(46, c["reading"]["battery"].as<int>());
    TEST_ASSERT_EQUAL(80, c["reading"]["limit"].as<int>());
    TEST_ASSERT_EQUAL(18, c["reading"]["amps"].as<int>());
    TEST_ASSERT_EQUAL(239, c["reading"]["volts"].as<int>());
    TEST_ASSERT_EQUAL(42, c["reading"]["ageSeconds"].as<int>());
    TEST_ASSERT_EQUAL(18, c["restoreAmps"].as<int>());
    TEST_ASSERT_TRUE(c["pair"].isNull());                       // only while pairing
    HttpResponse raw = r.api.handle("GET", "/api/telemetry", {});
    TEST_ASSERT_TRUE(raw.body.find("5YJYGDEE") == std::string::npos);   // the whole VIN never appears
}

void test_pairing_progress_and_the_last_result_are_in_telemetry() {
    Rig r;
    FakeCar car;
    r.api.setCar(&car);
    car.snap.busy = true;
    car.snap.job = "pair";
    car.snap.pairState = "waiting";
    car.snap.pairText = "Tap your key card.";
    car.snap.pairSecondsLeft = 90;
    car.snap.hasResult = true;
    car.snap.resultJob = "charge";
    car.snap.resultOk = true;
    car.snap.resultAgeSeconds = 5;
    car.snap.resultLines = {"Charging current set to 16 A."};
    JsonDocument t = r.telemetry();   // keep the document alive: JsonObject only refers to it
    JsonObject c = t["car"];
    testing::check("waiting", c["pair"]["state"] | "");
    TEST_ASSERT_EQUAL(90, c["pair"]["secondsLeft"].as<int>());
    TEST_ASSERT_TRUE(c["result"]["ok"].as<bool>());
    testing::check("Charging current set to 16 A.", c["result"]["lines"][0] | "");
}

void test_pairing_cleans_and_checks_the_vin_and_never_echoes_it() {
    Rig r;
    FakeCar car;
    r.api.setCar(&car);
    TEST_ASSERT_EQUAL(200, r.post("/api/car/pair", {{"vin", " 5yj-ygdee2 mf284667 "}}).status);
    testing::check("5YJYGDEE2MF284667", car.lastVin);          // spaces and dashes dropped, capitals
    testing::check("pair", car.lastCall);
    car.lastCall.clear();
    for (const char* bad : {"", "5YJYGDEE2MF28466", "5YJYGDEE2MF2846677", "5YJYGDEE2MF28466O", "5YJYGDEE2MF28466!"}) {
        HttpResponse reply = r.post("/api/car/pair", {{"vin", bad}});
        TEST_ASSERT_EQUAL(400, reply.status);
        TEST_ASSERT_TRUE(reply.body.find(bad[0] ? bad : "@@") == std::string::npos);   // not echoed
    }
    TEST_ASSERT_EQUAL(400, r.post("/api/car/pair", {}).status);
    testing::check("", car.lastCall);                           // nothing started
    car.refuseBusy = true;
    TEST_ASSERT_EQUAL(409, r.post("/api/car/pair", {{"vin", "5YJYGDEE2MF284667"}}).status);
}

void test_the_test_commands_need_a_paired_car_and_check_their_values() {
    Rig r;
    FakeCar car;
    r.api.setCar(&car);
    TEST_ASSERT_EQUAL(400, r.post("/api/car/test", {{"action", "stop"}}).status);     // not paired
    TEST_ASSERT_EQUAL(400, r.post("/api/car/refresh", {}).status);
    car.snap.paired = true;
    TEST_ASSERT_EQUAL(200, r.post("/api/car/test", {{"action", "start"}}).status);
    testing::check("start", car.lastCall);
    TEST_ASSERT_EQUAL(200, r.post("/api/car/test", {{"action", "stop"}}).status);
    testing::check("stop", car.lastCall);
    TEST_ASSERT_EQUAL(200, r.post("/api/car/test", {{"action", "amps"}, {"amps", "16"}}).status);
    TEST_ASSERT_EQUAL(16, car.lastAmps);
    car.lastCall.clear();
    for (const char* bad : {"5", "41", "abc", "", "-1"}) TEST_ASSERT_EQUAL(400, r.post("/api/car/test", {{"action", "amps"}, {"amps", bad}}).status);
    TEST_ASSERT_EQUAL(400, r.post("/api/car/test", {{"action", "amps"}}).status);
    TEST_ASSERT_EQUAL(400, r.post("/api/car/test", {{"action", "explode"}}).status);
    TEST_ASSERT_EQUAL(400, r.post("/api/car/test", {}).status);
    testing::check("", car.lastCall);                           // none of the bad ones reached the car
    TEST_ASSERT_EQUAL(200, r.post("/api/car/refresh", {}).status);
    testing::check("refresh", car.lastCall);
}

void test_watch_cancel_and_forget_work_and_car_posts_need_the_safe_header() {
    Rig r;
    FakeCar car;
    r.api.setCar(&car);
    car.snap.paired = true;
    TEST_ASSERT_EQUAL(200, r.post("/api/car/watch", {{"enabled", "false"}}).status);
    testing::check("watch-off", car.lastCall);
    TEST_ASSERT_FALSE(r.telemetry()["car"]["watch"].as<bool>());
    TEST_ASSERT_EQUAL(400, r.post("/api/car/watch", {{"enabled", "maybe"}}).status);
    TEST_ASSERT_EQUAL(400, r.post("/api/car/watch", {}).status);
    TEST_ASSERT_EQUAL(200, r.post("/api/car/cancel", {}).status);
    testing::check("cancel", car.lastCall);
    TEST_ASSERT_EQUAL(200, r.post("/api/car/forget", {}).status);
    testing::check("forget", car.lastCall);
    car.lastCall.clear();
    car.snap.paired = true;
    for (const char* path : {"/api/car/forget", "/api/car/test", "/api/car/pair", "/api/car/cancel", "/api/car/watch", "/api/car/refresh", "/api/car/through", "/api/car/policy"}) {
        TEST_ASSERT_EQUAL(403, r.api.handle("POST", path, {{"action", "stop"}, {"vin", "5YJYGDEE2MF284667"}, {"enabled", "true"}}, false).status);
    }
    testing::check("", car.lastCall);
    TEST_ASSERT_EQUAL(404, r.api.handle("GET", "/api/car/forget", {}).status);          // wrong method
}

void test_the_choice_of_who_is_in_command_is_validated_confirmed_and_shown() {
    Rig r;
    FakeCar car;
    r.api.setCar(&car);
    CarCommander commander(r.platform, car);
    ChargeRouter router(r.wallbox, commander, car);
    r.api.setRouter(&router);
    car.snap.paired = false;
    TEST_ASSERT_EQUAL(400, r.post("/api/car/through", {{"mode", "car"}, {"confirm", "yes"}}).status);   // not paired
    car.snap.paired = true;
    TEST_ASSERT_EQUAL(400, r.post("/api/car/through", {{"mode", "car"}}).status);                      // no confirmation
    TEST_ASSERT_EQUAL(400, r.post("/api/car/through", {{"mode", "car"}, {"confirm", "maybe"}}).status);
    TEST_ASSERT_EQUAL(400, r.post("/api/car/through", {{"mode", "boat"}}).status);
    TEST_ASSERT_EQUAL(400, r.post("/api/car/through", {}).status);
    TEST_ASSERT_FALSE(router.throughCar());
    {
        JsonDocument t = r.telemetry();
        JsonObject c = t["car"]["control"];
        TEST_ASSERT_FALSE(c["through"].as<bool>());
        testing::check("keep", c["onReturn"] | "");
    }
    TEST_ASSERT_EQUAL(200, r.post("/api/car/through", {{"mode", "car"}, {"confirm", "yes"}}).status);
    TEST_ASSERT_TRUE(router.throughCar());
    {
        JsonDocument t = r.telemetry();
        TEST_ASSERT_TRUE(t["car"]["control"]["through"].as<bool>());
        TEST_ASSERT_TRUE(t["car"]["control"]["state"].as<std::string>().size() > 0);
    }
    TEST_ASSERT_EQUAL(200, r.post("/api/car/through", {{"mode", "charger"}}).status);                  // going back needs no confirmation
    TEST_ASSERT_FALSE(router.throughCar());
}

void test_the_unreachable_policy_is_validated_and_shown() {
    Rig r;
    FakeCar car;
    r.api.setCar(&car);
    CarCommander commander(r.platform, car);
    ChargeRouter router(r.wallbox, commander, car);
    r.api.setRouter(&router);
    for (const char* ok : {"tell", "stop", "keep"}) TEST_ASSERT_EQUAL(200, r.post("/api/car/policy", {{"onReturn", ok}}).status);
    TEST_ASSERT_EQUAL(400, r.post("/api/car/policy", {{"onReturn", "panic"}}).status);
    TEST_ASSERT_EQUAL(400, r.post("/api/car/policy", {}).status);
    r.post("/api/car/policy", {{"onReturn", "stop"}});
    JsonDocument t = r.telemetry();
    testing::check("stop", t["car"]["control"]["onReturn"] | "");
}

void test_without_a_router_the_choice_endpoints_say_the_car_needs_the_esp32() {
    Rig r;
    FakeCar car;
    r.api.setCar(&car);
    TEST_ASSERT_EQUAL(400, r.post("/api/car/through", {{"mode", "charger"}}).status);
    TEST_ASSERT_EQUAL(400, r.post("/api/car/policy", {{"onReturn", "keep"}}).status);
    JsonDocument t = r.telemetry();
    TEST_ASSERT_TRUE(t["car"]["control"].isNull());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_choice_of_who_is_in_command_is_validated_confirmed_and_shown);
    RUN_TEST(test_the_unreachable_policy_is_validated_and_shown);
    RUN_TEST(test_without_a_router_the_choice_endpoints_say_the_car_needs_the_esp32);
    RUN_TEST(test_car_endpoints_say_not_supported_without_a_car_link);
    RUN_TEST(test_telemetry_shows_the_car_without_the_vin_or_key);
    RUN_TEST(test_pairing_progress_and_the_last_result_are_in_telemetry);
    RUN_TEST(test_pairing_cleans_and_checks_the_vin_and_never_echoes_it);
    RUN_TEST(test_the_test_commands_need_a_paired_car_and_check_their_values);
    RUN_TEST(test_watch_cancel_and_forget_work_and_car_posts_need_the_safe_header);
    RUN_TEST(test_telemetry_before_any_reading_or_charger);
    RUN_TEST(test_telemetry_shows_a_charging_car_and_the_latest_decision);
    RUN_TEST(test_status_names_follow_the_dashboard_convention);
    RUN_TEST(test_the_allowance_shown_by_the_controller_applies_to_eco_and_balanced_but_not_fast);
    RUN_TEST(test_telemetry_reports_an_envoy_problem_and_the_fail_safe);
    RUN_TEST(test_a_closed_window_is_reported);
    RUN_TEST(test_mode_can_be_changed);
    RUN_TEST(test_fast_mode_with_a_current_starts_a_waiting_car);
    RUN_TEST(test_a_bad_mode_or_current_changes_nothing);
    RUN_TEST(test_a_current_is_ignored_unless_the_mode_is_fast);
    RUN_TEST(test_the_allowance_is_validated);
    RUN_TEST(test_the_daylight_window_can_be_switched);
    RUN_TEST(test_the_maximum_current_limits_every_mode);
    RUN_TEST(test_the_departure_goal_endpoint_and_telemetry);
    RUN_TEST(test_notifications_are_set_tested_masked_and_saved);
    RUN_TEST(test_the_pause_endpoint_and_telemetry);
    RUN_TEST(test_pausing_through_the_api_stops_a_running_charge_and_is_saved);
    RUN_TEST(test_changes_through_the_api_are_saved);
    RUN_TEST(test_the_last_action_is_reported_in_plain_words_with_its_time);
    RUN_TEST(test_the_history_endpoint_returns_todays_samples_totals_and_events);
    RUN_TEST(test_the_history_endpoint_works_with_nothing_recorded_and_no_window);
    RUN_TEST(test_the_price_endpoint_and_telemetry);
    RUN_TEST(test_the_sessions_endpoint_lists_finished_and_running_sessions);
    RUN_TEST(test_history_carries_todays_day_number_so_times_can_be_labelled);
    RUN_TEST(test_a_past_day_comes_from_the_coarser_records);
    RUN_TEST(test_a_bad_daysago_is_refused);
    RUN_TEST(test_the_peak_price_and_its_hours_are_validated_applied_and_saved);
    RUN_TEST(test_the_days_endpoint_lists_daily_totals);
    RUN_TEST(test_the_daylight_margins_are_validated_applied_and_saved);
    RUN_TEST(test_the_csv_export_has_a_header_and_one_line_per_day);
    RUN_TEST(test_changing_requests_without_the_safe_header_are_refused);
    RUN_TEST(test_events_come_one_page_at_a_time_newest_first_with_the_total);
    RUN_TEST(test_sessions_can_be_paged_with_an_offset);
    RUN_TEST(test_diagnostics_are_404_without_a_source_and_list_only_what_the_platform_knows);
    RUN_TEST(test_unknown_paths_and_wrong_methods_are_not_found);
    return UNITY_END();
}
