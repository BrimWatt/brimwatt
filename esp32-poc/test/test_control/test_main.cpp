// Host-side tests for the solar controller: when it asks for readings, what it sends the wallbox, the outage
// fail-safe, the night pause and the saved settings.
#include "../support/TestSupport.h"

#include "control/SolarController.h"
#include "planner/GoalPlanner.h"
#include "daylight/DaylightSchedule.h"
#include "history/HistoryService.h"
#include "wallbox/ChargerRegistry.h"
#include "wallbox/WallboxController.h"

using namespace solar;
using testing::FakeLink;
using testing::FakePlatform;

namespace {

class MemoryStore : public SettingsStore {
public:
    std::optional<Settings> saved;
    int saves = 0;
    std::optional<Settings> load() override { return saved; }
    void save(const Settings& settings) override {
        saved = settings;
        saves++;
    }
};

class FakeSchedule : public DaylightSchedule {
public:
    bool open = true;
    bool isOpen(int64_t) const override { return open; }
    std::string describe(int64_t) const override { return "test window"; }
    int startSeconds = -1, endSeconds = -1;
    void setMargins(int start, int end) override { startSeconds = start; endSeconds = end; }
};

struct Rig {
    FakePlatform platform;
    FakeLink link;
    ChargerRegistry chargers;
    WallboxController wallbox{platform, chargers};
    SystemState state;
    FakeSchedule schedule;
    MemoryStore store;
    HistoryService history{platform};
    SolarController controller{platform, state, wallbox, schedule, store};
    Charger* charger;

    Rig() {
        platform.epoch = 1760000000;
        controller.setHistory(&history);
        chargers.add(1, "box", link, platform.nowMillis());
        charger = chargers.find(1);
    }

    /** True if the activity log has a line containing this text. */
    bool logged(const char* part) {
        for (const auto& e : history.recentEvents(50)) if (e.text.find(part) != std::string::npos) return true;
        return false;
    }

    void waiting() { charger->setStatus(ChargerStatus::Preparing); }
    void charging(int txn = 7, int amps = 12) {
        charger->setStatus(ChargerStatus::Charging);
        charger->setTransactionId(txn);
        charger->setAppliedAmps(amps);
    }

    /** One poll interval later: runs the checks and, if asked, feeds a good meter reading. */
    bool poll(double solarWatts, double gridWatts) {
        platform.now += 60000;
        platform.epoch += 60;
        if (!controller.tick()) return false;
        controller.onMeterResult({true, {solarWatts, gridWatts}, ""});
        return true;
    }

    /** One poll interval later with the Envoy unreachable. */
    void pollFailing() {
        platform.now += 60000;
        platform.epoch += 60;
        if (controller.tick()) controller.onMeterResult({false, {}, "timeout"});
    }

    int sentCount(const char* part) { return link.count(part); }
};

// ---- when readings are needed ----

void test_no_readings_are_requested_without_a_charger() {
    Rig r;
    r.chargers.remove(1);
    TEST_ASSERT_FALSE(r.controller.tick());
}

void test_custom_mode_reads_the_meters_for_the_dashboard_only() {
    Rig r;
    r.controller.setMode(ChargingMode::Fast);
    r.schedule.open = false;                           // the daylight window does not apply to Custom
    r.link.sent.clear();
    TEST_ASSERT_TRUE(r.poll(2000, 600));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 2000, r.state.solarWatts());
    TEST_ASSERT_TRUE(r.state.control().has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01, 600, r.state.control()->gridWatts);
    TEST_ASSERT_EQUAL(32, r.state.control()->targetAmps);
    TEST_ASSERT_EQUAL(0, r.link.sent.size());           // nothing is ever sent because of a reading in Custom
}

void test_custom_mode_does_not_read_without_a_charger() {
    Rig r;
    r.controller.setMode(ChargingMode::Fast);
    r.chargers.remove(1);
    TEST_ASSERT_FALSE(r.controller.tick());
}

void test_a_gateway_problem_in_custom_mode_is_shown_but_is_never_a_fail_safe() {
    Rig r;
    r.controller.setMode(ChargingMode::Fast);
    r.charging(7, 32);
    for (int i = 0; i < 6; i++) r.pollFailing();
    TEST_ASSERT_FALSE(r.state.enphaseOk());
    TEST_ASSERT_FALSE(r.state.failSafe());
    TEST_ASSERT_EQUAL(0, r.sentCount("\"RemoteStopTransaction\""));
    r.poll(1000, 0);
    TEST_ASSERT_TRUE(r.state.enphaseOk());
}

void test_readings_are_requested_in_eco_and_balanced_by_day() {
    Rig r;
    TEST_ASSERT_TRUE(r.controller.tick());
    r.controller.setMode(ChargingMode::MaxSolar);
    TEST_ASSERT_TRUE(r.controller.tick());
}

void test_a_charging_car_is_asked_for_its_real_current_each_poll() {
    Rig r;
    r.charging();
    r.poll(5000, -3000);
    r.poll(5000, -3000);
    TEST_ASSERT_EQUAL(2, r.sentCount("\"MeterValues\""));
}

// ---- ECO ----

void test_eco_starts_a_waiting_car_on_real_surplus() {
    Rig r;
    r.waiting();
    for (int i = 0; i < 4; i++) r.poll(5000, -2500);   // 2.5 kW export plus the 1 kW allowance is about 14 A, held for three minutes
    TEST_ASSERT_EQUAL(1, r.sentCount("\"RemoteStartTransaction\""));
}

void test_eco_does_not_start_without_surplus() {
    Rig r;
    r.waiting();
    for (int i = 0; i < 10; i++) r.poll(500, 300);
    TEST_ASSERT_EQUAL(0, r.link.sent.size());
}

void test_eco_stops_a_charging_car_when_the_surplus_vanishes() {
    Rig r;
    r.charging(7, 12);
    // Importing 3 kW while the car draws 12 A: 12 A + (-3000 + 1000) W / 240 V is under the 6 A minimum.
    for (int i = 0; i < 8; i++) r.poll(300, 3000);     // it must last five minutes before the charge stops
    TEST_ASSERT_EQUAL(1, r.sentCount("\"RemoteStopTransaction\""));
}

// ---- MAX SOLAR ----

void test_max_solar_follows_the_sun_and_ignores_the_house() {
    Rig r;
    r.controller.setMode(ChargingMode::MaxSolar);
    r.waiting();
    for (int i = 0; i < 4; i++) r.poll(3000, 1000);    // 3 kW of sun while the house imports 1 kW: Eco would not start
    TEST_ASSERT_EQUAL(1, r.sentCount("\"RemoteStartTransaction\""));
}

void test_max_solar_tops_up_within_the_allowance_and_stays_off_beyond_it() {
    Rig r;
    r.controller.setMode(ChargingMode::MaxSolar);
    r.controller.setGridAllowance(0);
    r.waiting();
    for (int i = 0; i < 6; i++) r.poll(1000, 200);     // about 4.2 A of sun and no allowance to top it up
    TEST_ASSERT_EQUAL(0, r.sentCount("\"RemoteStartTransaction\""));

    r.controller.setGridAllowance(600);                // 6 A needs about 440 W more: that fits
    for (int i = 0; i < 4; i++) r.poll(1000, 200);     // held for three minutes, then it starts
    TEST_ASSERT_EQUAL(1, r.sentCount("\"RemoteStartTransaction\""));
}

void test_changing_the_allowance_shows_up_on_the_next_reading() {
    Rig r;
    r.controller.setGridAllowance(0);
    r.waiting();
    for (int i = 0; i < 3; i++) r.poll(2000, -1000);   // about 4.2 A available
    r.controller.setGridAllowance(1000);
    r.poll(2000, -1000);                               // now about 8.3 A
    TEST_ASSERT_TRUE(r.state.control().has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.05, 8.33, r.state.control()->smoothedAmps);
}

// ---- Envoy outage ----

void test_an_outage_stops_charging_after_the_fail_safe_delay_and_recovers() {
    Rig r;
    r.charging(7, 12);
    r.poll(5000, -3000);
    r.pollFailing();                                    // the outage starts here
    r.pollFailing();
    r.pollFailing();                                    // two minutes without data
    TEST_ASSERT_FALSE(r.state.failSafe());
    TEST_ASSERT_EQUAL(0, r.sentCount("\"RemoteStopTransaction\""));
    r.pollFailing();                                    // three minutes: past the 150 s fail-safe delay
    TEST_ASSERT_TRUE(r.state.failSafe());
    TEST_ASSERT_TRUE(r.sentCount("\"RemoteStopTransaction\"") >= 1);
    TEST_ASSERT_FALSE(r.state.enphaseOk());

    r.charger->setStatus(ChargerStatus::Charging);
    r.poll(5000, -3000);
    TEST_ASSERT_FALSE(r.state.failSafe());
    TEST_ASSERT_TRUE(r.state.enphaseOk());
}

void test_balanced_also_stops_in_an_outage() {
    Rig r;
    r.controller.setMode(ChargingMode::MaxSolar);
    r.charging(7, 14);
    for (int i = 0; i < 4; i++) r.pollFailing();
    TEST_ASSERT_TRUE(r.sentCount("\"RemoteStopTransaction\"") >= 1);
}

// ---- night ----

void test_outside_daylight_the_charge_is_stopped_and_no_readings_are_requested() {
    Rig r;
    r.schedule.open = false;
    r.charging(7, 12);
    r.platform.now += 60000;
    TEST_ASSERT_FALSE(r.controller.tick());
    TEST_ASSERT_EQUAL(1, r.sentCount("\"RemoteStopTransaction\""));
    TEST_ASSERT_EQUAL(0, r.sentCount("\"MeterValues\""));
}

void test_the_daylight_window_can_be_switched_off() {
    Rig r;
    r.schedule.open = false;
    r.controller.setDaylightWindowEnabled(false);
    TEST_ASSERT_TRUE(r.controller.tick());
}

void test_fast_mode_is_not_affected_by_the_window() {
    Rig r;
    r.schedule.open = false;
    r.waiting();
    r.controller.setMode(ChargingMode::Fast);
    TEST_ASSERT_EQUAL(1, r.sentCount("\"RemoteStartTransaction\""));
}

// ---- FAST (custom) ----

void test_switching_to_fast_starts_a_waiting_car_at_the_custom_current() {
    Rig r;
    r.waiting();
    r.controller.setFastAmps(20);
    r.controller.setMode(ChargingMode::Fast);
    TEST_ASSERT_EQUAL(1, r.sentCount("\"RemoteStartTransaction\""));
    TEST_ASSERT_TRUE(testing::contains(r.link.last(), "\"limit\":20"));
}

void test_changing_the_custom_current_while_charging_sends_a_profile() {
    Rig r;
    r.charging(7, 20);
    r.controller.setMode(ChargingMode::Fast);   // already at 32 A default: sends a profile
    r.controller.setFastAmps(16);
    TEST_ASSERT_TRUE(testing::contains(r.link.last(), "\"SetChargingProfile\""));
    TEST_ASSERT_TRUE(testing::contains(r.link.last(), "\"limit\":16"));
}

void test_a_car_plugged_in_later_is_started_in_fast_mode() {
    Rig r;
    r.controller.setMode(ChargingMode::Fast);
    TEST_ASSERT_EQUAL(0, r.link.sent.size());
    r.charger->setStatus(ChargerStatus::Preparing);
    r.wallbox.onChargerChanged(*r.charger);
    TEST_ASSERT_EQUAL(1, r.sentCount("\"RemoteStartTransaction\""));
}

void test_leaving_fast_mode_stops_forcing_the_custom_current() {
    Rig r;
    r.controller.setMode(ChargingMode::Fast);
    r.controller.setMode(ChargingMode::Eco);
    r.charger->setStatus(ChargerStatus::Preparing);
    r.wallbox.onChargerChanged(*r.charger);
    TEST_ASSERT_EQUAL(0, r.sentCount("\"RemoteStartTransaction\""));
}

void test_custom_current_and_allowance_are_range_checked() {
    Rig r;
    TEST_ASSERT_FALSE(r.controller.setFastAmps(5));
    TEST_ASSERT_FALSE(r.controller.setFastAmps(41));
    TEST_ASSERT_TRUE(r.controller.setFastAmps(40));
    TEST_ASSERT_FALSE(r.controller.setGridAllowance(-1));
    TEST_ASSERT_FALSE(r.controller.setGridAllowance(1001));
    TEST_ASSERT_TRUE(r.controller.setGridAllowance(500));
    TEST_ASSERT_EQUAL(40, r.state.fastModeAmps());
    TEST_ASSERT_EQUAL(500, r.state.gridAllowanceWatts());
}

void test_stop_now_stops_a_charging_car() {
    Rig r;
    r.charging();
    r.controller.stopNow();
    TEST_ASSERT_EQUAL(1, r.sentCount("\"RemoteStopTransaction\""));
}

// ---- the kill switch (pause) ----

void test_pausing_stops_a_charging_car_at_once_in_every_mode_and_keeps_the_mode() {
    for (ChargingMode mode : {ChargingMode::Eco, ChargingMode::MaxSolar, ChargingMode::Fast}) {
        Rig r;
        r.controller.setMode(mode);
        r.link.sent.clear();
        r.charging(7, 14);
        r.controller.setPaused(true);
        TEST_ASSERT_EQUAL(1, r.sentCount("\"RemoteStopTransaction\""));
        TEST_ASSERT_TRUE(r.state.paused());
        TEST_ASSERT_EQUAL(static_cast<int>(mode), static_cast<int>(r.state.mode()));   // pausing never changes the mode
    }
}

void test_a_paused_system_never_starts_a_charge() {
    Rig r;
    r.controller.setPaused(true);
    r.waiting();
    for (int i = 0; i < 6; i++) r.poll(8000, -5000);     // plenty of sun in ECO
    r.wallbox.onChargerChanged(*r.charger);
    r.controller.setMode(ChargingMode::Fast);            // even a mode change while paused starts nothing
    r.controller.setFastAmps(20);
    TEST_ASSERT_EQUAL(0, r.sentCount("\"RemoteStartTransaction\""));
}

void test_a_car_that_starts_by_itself_while_paused_is_stopped_again() {
    Rig r;
    r.controller.setPaused(true);
    r.charging(9, 10);                                   // e.g. started from the wallbox's own app
    r.wallbox.onChargerChanged(*r.charger);
    TEST_ASSERT_EQUAL(1, r.sentCount("\"RemoteStopTransaction\""));
}

void test_a_stop_is_kept_in_force_every_poll_while_paused() {
    Rig r;
    r.controller.setPaused(true);
    r.charging(9, 10);
    r.poll(5000, -3000);
    r.poll(5000, -3000);
    TEST_ASSERT_TRUE(r.sentCount("\"RemoteStopTransaction\"") >= 2);
}

void test_the_dashboard_stays_live_while_paused_and_a_dead_envoy_is_not_a_fail_safe() {
    Rig r;
    r.controller.setPaused(true);
    TEST_ASSERT_TRUE(r.poll(2000, 500));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 2000, r.state.solarWatts());
    testing::check("paused", r.state.control()->reason);
    TEST_ASSERT_EQUAL(0, r.state.control()->targetAmps);

    for (int i = 0; i < 6; i++) r.pollFailing();
    TEST_ASSERT_FALSE(r.state.enphaseOk());
    TEST_ASSERT_FALSE(r.state.failSafe());
}

void test_resuming_hands_the_charger_back_to_the_saved_mode() {
    Rig r;
    r.controller.setMode(ChargingMode::MaxSolar);
    r.controller.setPaused(true);
    r.waiting();
    for (int i = 0; i < 4; i++) r.poll(8000, -4000);
    TEST_ASSERT_EQUAL(0, r.sentCount("\"RemoteStartTransaction\""));

    r.controller.setPaused(false);
    TEST_ASSERT_FALSE(r.state.paused());
    TEST_ASSERT_EQUAL(static_cast<int>(ChargingMode::MaxSolar), static_cast<int>(r.state.mode()));
    for (int i = 0; i < 4; i++) r.poll(8000, -4000);
    TEST_ASSERT_EQUAL(1, r.sentCount("\"RemoteStartTransaction\""));   // Max solar plans again
}

void test_resuming_in_custom_mode_reapplies_the_fixed_current() {
    Rig r;
    r.controller.setFastAmps(16);
    r.controller.setMode(ChargingMode::Fast);
    r.controller.setPaused(true);
    r.waiting();
    r.link.sent.clear();
    r.controller.setPaused(false);
    TEST_ASSERT_EQUAL(1, r.sentCount("\"RemoteStartTransaction\""));
    TEST_ASSERT_TRUE(testing::contains(r.link.last(), "\"limit\":16"));
}

void test_the_pause_is_saved_and_still_holds_after_a_restart() {
    Rig first;
    first.controller.setPaused(true);
    TEST_ASSERT_TRUE(first.store.saved->paused);

    Rig second;                                          // a restarted board with the same flash contents
    second.store.saved = first.store.saved;
    second.controller.restoreSettings();
    TEST_ASSERT_TRUE(second.state.paused());

    second.charger->setStatus(ChargerStatus::Preparing); // the wallbox reconnects with a waiting car
    second.wallbox.onChargerChanged(*second.charger);
    TEST_ASSERT_EQUAL(0, second.sentCount("\"RemoteStartTransaction\""));

    second.charging(9, 10);                              // and one that is charging gets stopped
    second.wallbox.onChargerChanged(*second.charger);
    TEST_ASSERT_EQUAL(1, second.sentCount("\"RemoteStopTransaction\""));
}

void test_resuming_is_saved_too() {
    Rig first;
    first.controller.setPaused(true);
    first.controller.setPaused(false);
    Rig second;
    second.store.saved = first.store.saved;
    second.controller.restoreSettings();
    TEST_ASSERT_FALSE(second.state.paused());
}

// ---- history, the activity log and the electricity price ----

void test_every_reading_adds_a_sample_with_the_car_share() {
    Rig r;
    r.charging(7, 10);
    r.poll(3000, -300);                      // 10 A assumed from the limit: 2400 W for the car
    auto samples = r.history.series(0, 60);
    TEST_ASSERT_EQUAL(1, samples.size());
    TEST_ASSERT_EQUAL(3000, samples[0].solar);
    TEST_ASSERT_TRUE(samples[0].car >= 2000);
}

void test_readings_in_custom_mode_and_while_paused_are_recorded_too() {
    Rig r;
    r.controller.setMode(ChargingMode::Fast);
    r.poll(2000, 500);
    r.controller.setPaused(true);
    r.poll(2000, 500);
    auto samples = r.history.series(0, 60);
    TEST_ASSERT_EQUAL(2, samples.size());
    TEST_ASSERT_EQUAL(2, samples[0].mode);
    TEST_ASSERT_TRUE(samples[1].flags & kFlagPaused);
}

void test_the_activity_log_says_what_changed_and_why() {
    Rig r;
    r.controller.setMode(ChargingMode::MaxSolar);
    r.controller.setGridAllowance(300);
    r.controller.setFastAmps(16);
    r.controller.setPaused(true);
    r.controller.setPaused(false);
    TEST_ASSERT_TRUE(r.logged("Mode set to Max solar"));
    TEST_ASSERT_TRUE(r.logged("Grid allowance set to 300 W"));
    TEST_ASSERT_TRUE(r.logged("Custom speed set to 16 A"));
    TEST_ASSERT_TRUE(r.logged("Charging paused by you"));
    TEST_ASSERT_TRUE(r.logged("Charging resumed (Max solar)"));
}

void test_a_start_and_a_stop_are_logged_with_their_reason() {
    Rig r;
    r.waiting();
    for (int i = 0; i < 4; i++) r.poll(5000, -2500);
    TEST_ASSERT_TRUE(r.logged("Started charging at"));
    TEST_ASSERT_TRUE(r.logged("enough solar"));

    r.charging(7, 14);
    r.platform.epoch += 600;
    for (int i = 0; i < 8; i++) r.poll(3000, 4500);   // the oven: solar is fine but the house takes more than solar + allowance
    TEST_ASSERT_TRUE(r.logged("Stopped charging: home is using the solar"));
}

void test_a_stop_with_little_sun_says_not_enough_solar() {
    Rig r;
    r.charging(7, 12);
    for (int i = 0; i < 8; i++) r.poll(300, 3000);    // dusk
    TEST_ASSERT_TRUE(r.logged("Stopped charging: not enough solar"));
}

void test_a_pause_stop_and_the_fail_safe_are_explained() {
    Rig r;
    r.charging(7, 12);
    r.controller.setPaused(true);
    TEST_ASSERT_TRUE(r.logged("Stopped charging: paused by you"));

    Rig q;
    q.charging(7, 12);
    q.poll(5000, -3000);
    for (int i = 0; i < 4; i++) q.pollFailing();
    TEST_ASSERT_TRUE(q.logged("Lost contact with the solar meter: charging stopped"));
    q.charger->setStatus(ChargerStatus::Charging);
    q.poll(5000, -3000);
    TEST_ASSERT_TRUE(q.logged("Solar meter is back"));
}

void test_the_electricity_price_is_validated_saved_and_logged() {
    Rig r;
    TEST_ASSERT_FALSE(r.controller.setRateCents(-1));
    TEST_ASSERT_FALSE(r.controller.setRateCents(201));
    TEST_ASSERT_TRUE(r.controller.setRateCents(34));
    TEST_ASSERT_EQUAL(34, r.state.rateCents());
    TEST_ASSERT_EQUAL(34, r.store.saved->rateCents);
    TEST_ASSERT_TRUE(r.logged("Price set to 34¢ per kWh"));

    Rig second;
    second.store.saved = r.store.saved;
    second.controller.restoreSettings();
    TEST_ASSERT_EQUAL(34, second.state.rateCents());
}

void test_the_default_price_is_thirty_cents() {
    Rig r;
    r.controller.restoreSettings();
    TEST_ASSERT_EQUAL(30, r.state.rateCents());
}

// ---- saved settings ----

void test_settings_are_saved_on_every_change_and_restored_after_a_restart() {
    Rig first;
    first.controller.setMode(ChargingMode::MaxSolar);
    first.controller.setFastAmps(20);
    first.controller.setGridAllowance(300);
    first.controller.setDaylightWindowEnabled(false);
    TEST_ASSERT_EQUAL(4, first.store.saves);

    Rig second;
    second.store.saved = first.store.saved;
    second.controller.restoreSettings();
    TEST_ASSERT_EQUAL(static_cast<int>(ChargingMode::MaxSolar), static_cast<int>(second.state.mode()));
    TEST_ASSERT_EQUAL(20, second.state.fastModeAmps());
    TEST_ASSERT_EQUAL(300, second.state.gridAllowanceWatts());
    TEST_ASSERT_FALSE(second.state.daylightWindowEnabled());
}

void test_saved_daylight_margins_are_restored_and_given_to_the_window() {
    Rig first;
    TEST_ASSERT_TRUE(first.controller.setDaylightMargins(20, 90));
    TEST_ASSERT_FALSE(first.controller.setDaylightMargins(-1, 60));
    TEST_ASSERT_FALSE(first.controller.setDaylightMargins(30, 181));
    TEST_ASSERT_EQUAL(20, first.state.daylightStartMinutes());   // rejected values change nothing

    Rig second;
    second.store.saved = first.store.saved;
    second.controller.restoreSettings();
    TEST_ASSERT_EQUAL(20, second.state.daylightStartMinutes());
    TEST_ASSERT_EQUAL(90, second.state.daylightEndMinutes());
    TEST_ASSERT_EQUAL(1200, second.schedule.startSeconds);
    TEST_ASSERT_EQUAL(5400, second.schedule.endSeconds);
}

void test_nothing_saved_means_defaults() {
    Rig r;
    r.controller.restoreSettings();
    TEST_ASSERT_EQUAL(static_cast<int>(ChargingMode::Eco), static_cast<int>(r.state.mode()));
    TEST_ASSERT_EQUAL(32, r.state.fastModeAmps());
    TEST_ASSERT_EQUAL(1000, r.state.gridAllowanceWatts());
    TEST_ASSERT_TRUE(r.state.daylightWindowEnabled());
}

void test_out_of_range_saved_values_are_ignored() {
    Rig r;
    Settings bad;
    bad.fastModeAmps = 99;
    bad.gridAllowanceWatts = 9000;
    r.store.saved = bad;
    r.controller.restoreSettings();
    TEST_ASSERT_EQUAL(32, r.state.fastModeAmps());
    TEST_ASSERT_EQUAL(1000, r.state.gridAllowanceWatts());
}

void test_a_restored_fast_mode_takes_effect_when_the_charger_reports() {
    Rig r;
    Settings saved;
    saved.mode = ChargingMode::Fast;
    saved.fastModeAmps = 12;
    r.store.saved = saved;
    r.controller.restoreSettings();
    r.charger->setStatus(ChargerStatus::Preparing);
    r.wallbox.onChargerChanged(*r.charger);
    TEST_ASSERT_EQUAL(1, r.sentCount("\"RemoteStartTransaction\""));
    TEST_ASSERT_TRUE(testing::contains(r.link.last(), "\"limit\":12"));
}

}  // namespace

// ---- Cheapest hours ----

void test_cheapest_hours_charges_at_the_maximum_in_the_cheap_hours_without_the_sun() {
    Rig r;
    r.platform.epoch = 1762405200;   // Wednesday 10 pm local
    r.controller.setMode(ChargingMode::Cheapest);
    r.waiting();
    r.link.sent.clear();
    TEST_ASSERT_TRUE(r.poll(0, 0));                      // night, no solar: irrelevant
    TEST_ASSERT_TRUE(r.state.control().has_value());
    TEST_ASSERT_EQUAL(r.state.maxAmps(), r.state.control()->targetAmps);
    TEST_ASSERT_EQUAL_STRING("cheap hours", r.state.control()->reason.c_str());
    TEST_ASSERT_TRUE(r.sentCount("RemoteStartTransaction") >= 1);
}

void test_cheapest_hours_stops_when_the_expensive_hours_begin() {
    Rig r;
    r.platform.epoch = 1762380000;   // Wednesday 3 pm local
    r.controller.setMode(ChargingMode::Cheapest);
    r.charging(7, r.state.maxAmps());
    r.poll(0, 0);
    r.platform.epoch = 1762387200;   // 5 pm: inside the default 4-9 pm window
    r.link.sent.clear();
    r.poll(0, 0);
    TEST_ASSERT_EQUAL(0, r.state.control()->targetAmps);
    TEST_ASSERT_EQUAL_STRING("expensive hours", r.state.control()->reason.c_str());
    TEST_ASSERT_TRUE(r.sentCount("RemoteStopTransaction") >= 1);
    TEST_ASSERT_TRUE(r.logged("Expensive hours began"));
}

void test_cheapest_hours_never_trips_the_meter_fail_safe() {
    Rig r;
    r.platform.epoch = 1762405200;
    r.controller.setMode(ChargingMode::Cheapest);
    r.charging(7, r.state.maxAmps());
    for (int i = 0; i < 10; i++) r.pollFailing();
    TEST_ASSERT_FALSE(r.state.failSafe());
}

void test_weekends_are_cheap_all_day_when_the_window_is_weekdays_only() {
    Rig r;
    r.platform.epoch = 1762646400;   // Saturday 5 pm local
    r.controller.setPrices(30, 0, 16, 21, false);
    r.controller.setMode(ChargingMode::Cheapest);
    r.waiting();
    r.poll(0, 0);
    TEST_ASSERT_EQUAL(r.state.maxAmps(), r.state.control()->targetAmps);
    TEST_ASSERT_FALSE(r.state.peakWeekends());
    r.controller.setPrices(30, 0, 16, 21, true);
    TEST_ASSERT_TRUE(r.state.peakWeekends());
}

void test_the_weekend_choice_is_saved_and_restored() {
    Rig r;
    r.controller.setPrices(30, 0, 16, 21, false);
    TEST_ASSERT_TRUE(r.store.saved.has_value());
    TEST_ASSERT_FALSE(r.store.saved->peakWeekends);
    SystemState fresh;
    Rig r2;
    r2.store.saved = r.store.saved;
    r2.controller.restoreSettings();
    TEST_ASSERT_FALSE(r2.state.peakWeekends());
}

void setUp() {}
void tearDown() {}

// ---- the departure goal ----

int minutesFromNow(Rig& r, int minutes) {
    int64_t local = r.platform.epoch + r.platform.offsetSeconds;
    return static_cast<int>((((local % 86400) + minutes * 60) % 86400) / 60);
}

void test_the_departure_goal_waits_while_there_is_time_then_takes_over_even_at_night() {
    Rig r;
    r.waiting();
    r.schedule.open = false;                                 // night: normally nothing charges
    TEST_ASSERT_TRUE(r.controller.setGoal(10, minutesFromNow(r, 6 * 60)));
    r.poll(0, 0);
    TEST_ASSERT_FALSE(r.state.goalForcing());                // six hours left for 10 kWh: still waiting for the sun

    TEST_ASSERT_TRUE(r.controller.setGoal(10, minutesFromNow(r, 60)));
    TEST_ASSERT_TRUE(r.poll(0, 0));                          // the meters are still read for the dashboard
    TEST_ASSERT_TRUE(r.state.goalForcing());
    TEST_ASSERT_TRUE(r.state.goalForcedAmps() > config::kMinAmps);
    TEST_ASSERT_EQUAL(r.state.goalForcedAmps(), r.state.control()->targetAmps);
    TEST_ASSERT_TRUE(r.logged("Departure goal needs charging now"));
}

void test_the_departure_goal_needs_a_car_and_is_ignored_when_off_or_paused() {
    Rig r;
    r.schedule.open = false;
    r.controller.setGoal(10, minutesFromNow(r, 30));
    r.poll(0, 0);
    TEST_ASSERT_FALSE(r.state.goalForcing());                // no car plugged in
    r.waiting();
    r.controller.setPaused(true);
    r.poll(0, 0);
    TEST_ASSERT_FALSE(r.state.goalForcing());                // the kill switch wins
    r.controller.setPaused(false);
    r.controller.setGoal(0, minutesFromNow(r, 30));
    r.poll(0, 0);
    TEST_ASSERT_FALSE(r.state.goalForcing());                // turned off
}

void test_the_departure_goal_is_range_checked_saved_and_restored() {
    Rig r;
    TEST_ASSERT_FALSE(r.controller.setGoal(101, 420));
    TEST_ASSERT_FALSE(r.controller.setGoal(-1, 420));
    TEST_ASSERT_FALSE(r.controller.setGoal(10, 1440));
    TEST_ASSERT_TRUE(r.controller.setGoal(25, 6 * 60 + 30));
    TEST_ASSERT_EQUAL(25, r.store.saved->goalKwh);
    TEST_ASSERT_EQUAL(390, r.store.saved->goalMinutes);
    TEST_ASSERT_TRUE(r.logged("Departure goal: 25 kWh by 6:30"));
    Rig again;
    again.store.saved = r.store.saved;
    again.controller.restoreSettings();
    TEST_ASSERT_EQUAL(25, again.state.goalKwh());
    TEST_ASSERT_EQUAL(390, again.state.goalMinutes());
}

void test_goal_arithmetic() {
    using namespace solar::goal;
    TEST_ASSERT_EQUAL(0, decide(0, 0, 3600, 6, 32, 240).forcedAmps);
    TEST_ASSERT_EQUAL(0, decide(10000, 0, 6 * 3600, 6, 32, 240).forcedAmps);
    TEST_ASSERT_EQUAL(28, decide(10000, 0, 5400, 6, 32, 240).forcedAmps);
    TEST_ASSERT_EQUAL(6, decide(300, 0, 800, 6, 32, 240).forcedAmps);
    Decision d = decide(50000, 0, 3600, 6, 32, 240);
    TEST_ASSERT_EQUAL(32, d.forcedAmps);
    TEST_ASSERT_TRUE(d.unreachable);
    TEST_ASSERT_TRUE(decide(10000, 10500, 600, 6, 32, 240).met);
    int64_t base = 1760000000;   // 08:53:20 UTC
    int64_t secOfDay = base % 86400;
    TEST_ASSERT_EQUAL(base + (9 * 3600 - secOfDay), nextDeadline(base, 0, 9 * 60));
    TEST_ASSERT_EQUAL(base + (86400 - secOfDay), nextDeadline(base, 0, 0));   // midnight is tomorrow's
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_no_readings_are_requested_without_a_charger);
    RUN_TEST(test_custom_mode_reads_the_meters_for_the_dashboard_only);
    RUN_TEST(test_custom_mode_does_not_read_without_a_charger);
    RUN_TEST(test_a_gateway_problem_in_custom_mode_is_shown_but_is_never_a_fail_safe);
    RUN_TEST(test_readings_are_requested_in_eco_and_balanced_by_day);
    RUN_TEST(test_a_charging_car_is_asked_for_its_real_current_each_poll);
    RUN_TEST(test_eco_starts_a_waiting_car_on_real_surplus);
    RUN_TEST(test_eco_does_not_start_without_surplus);
    RUN_TEST(test_eco_stops_a_charging_car_when_the_surplus_vanishes);
    RUN_TEST(test_max_solar_follows_the_sun_and_ignores_the_house);
    RUN_TEST(test_max_solar_tops_up_within_the_allowance_and_stays_off_beyond_it);
    RUN_TEST(test_changing_the_allowance_shows_up_on_the_next_reading);
    RUN_TEST(test_an_outage_stops_charging_after_the_fail_safe_delay_and_recovers);
    RUN_TEST(test_balanced_also_stops_in_an_outage);
    RUN_TEST(test_outside_daylight_the_charge_is_stopped_and_no_readings_are_requested);
    RUN_TEST(test_the_daylight_window_can_be_switched_off);
    RUN_TEST(test_fast_mode_is_not_affected_by_the_window);
    RUN_TEST(test_switching_to_fast_starts_a_waiting_car_at_the_custom_current);
    RUN_TEST(test_changing_the_custom_current_while_charging_sends_a_profile);
    RUN_TEST(test_a_car_plugged_in_later_is_started_in_fast_mode);
    RUN_TEST(test_leaving_fast_mode_stops_forcing_the_custom_current);
    RUN_TEST(test_custom_current_and_allowance_are_range_checked);
    RUN_TEST(test_stop_now_stops_a_charging_car);
    RUN_TEST(test_pausing_stops_a_charging_car_at_once_in_every_mode_and_keeps_the_mode);
    RUN_TEST(test_a_paused_system_never_starts_a_charge);
    RUN_TEST(test_a_car_that_starts_by_itself_while_paused_is_stopped_again);
    RUN_TEST(test_a_stop_is_kept_in_force_every_poll_while_paused);
    RUN_TEST(test_the_dashboard_stays_live_while_paused_and_a_dead_envoy_is_not_a_fail_safe);
    RUN_TEST(test_resuming_hands_the_charger_back_to_the_saved_mode);
    RUN_TEST(test_resuming_in_custom_mode_reapplies_the_fixed_current);
    RUN_TEST(test_the_pause_is_saved_and_still_holds_after_a_restart);
    RUN_TEST(test_resuming_is_saved_too);
    RUN_TEST(test_every_reading_adds_a_sample_with_the_car_share);
    RUN_TEST(test_readings_in_custom_mode_and_while_paused_are_recorded_too);
    RUN_TEST(test_the_activity_log_says_what_changed_and_why);
    RUN_TEST(test_a_start_and_a_stop_are_logged_with_their_reason);
    RUN_TEST(test_a_stop_with_little_sun_says_not_enough_solar);
    RUN_TEST(test_a_pause_stop_and_the_fail_safe_are_explained);
    RUN_TEST(test_the_electricity_price_is_validated_saved_and_logged);
    RUN_TEST(test_the_default_price_is_thirty_cents);
    RUN_TEST(test_settings_are_saved_on_every_change_and_restored_after_a_restart);
    RUN_TEST(test_nothing_saved_means_defaults);
    RUN_TEST(test_out_of_range_saved_values_are_ignored);
    RUN_TEST(test_a_restored_fast_mode_takes_effect_when_the_charger_reports);
    RUN_TEST(test_saved_daylight_margins_are_restored_and_given_to_the_window);
    RUN_TEST(test_the_departure_goal_waits_while_there_is_time_then_takes_over_even_at_night);
    RUN_TEST(test_the_departure_goal_needs_a_car_and_is_ignored_when_off_or_paused);
    RUN_TEST(test_the_departure_goal_is_range_checked_saved_and_restored);
    RUN_TEST(test_goal_arithmetic);
    RUN_TEST(test_cheapest_hours_charges_at_the_maximum_in_the_cheap_hours_without_the_sun);
    RUN_TEST(test_cheapest_hours_stops_when_the_expensive_hours_begin);
    RUN_TEST(test_cheapest_hours_never_trips_the_meter_fail_safe);
    RUN_TEST(test_weekends_are_cheap_all_day_when_the_window_is_weekdays_only);
    RUN_TEST(test_the_weekend_choice_is_saved_and_restored);
    return UNITY_END();
}
