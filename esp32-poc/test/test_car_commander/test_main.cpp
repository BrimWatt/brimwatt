// Host tests for "Control through: Car": the commander that turns the planner's wish into Bluetooth jobs, its rules for a car that cannot
// be reached, and the router that makes sure only one device is in command.
#include "../support/TestSupport.h"

#include "car/CarCommander.h"
#include "car/ChargeRouter.h"
#include "control/SolarController.h"
#include "history/HistoryService.h"

using namespace solar;
using testing::FakePlatform;

void setUp() {}
void tearDown() {}

namespace {

/** A pretend Bluetooth side: jobs are recorded and finish when the test says so. */
class FakeCar : public CarControl {
public:
    CarSnapshot snap;
    std::vector<std::string> jobs;
    bool quietReads = false;

    FakeCar() {
        snap.paired = true;
        snap.vin = "5YJ**********4667";
    }

    CarSnapshot snapshot() const override { return snap; }
    CarResult begin(const std::string& what) {
        CarResult r;
        if (snap.busy) { r.ok = false; r.busy = true; r.message = "busy"; return r; }
        jobs.push_back(what);
        snap.busy = true;
        snap.job = what.rfind("read", 0) == 0 ? "status" : what == "wake" ? "wake" : "charge";
        return r;
    }
    CarResult startPair(const std::string&) override { return begin("pair"); }
    CarResult startRefresh() override { return begin("read"); }
    CarResult startQuietRefresh() override { quietReads = true; return begin("read"); }
    CarResult startStart() override { return begin("start"); }
    CarResult startStop() override { return begin("stop"); }
    CarResult startWake() override { return begin("wake"); }
    CarResult startSetAmps(int amps) override { return begin("amps " + std::to_string(amps)); }
    void cancel() override {}
    void setWatch(bool) override {}
    void forget() override { snap.paired = false; }
    bool controlled = false;
    void setControlled(bool on) override { controlled = on; }

    // ---- test helpers ----
    void setCar(int chargingCode, int requestAmps, int actualAmps, int sleep = 1) {
        snap.hasReading = true;
        snap.readingAgeSeconds = 0;
        snap.sleep = sleep;
        snap.charge = tesla::ChargeState();
        snap.charge.valid = true;
        snap.charge.chargingState = chargingCode;
        snap.charge.hasCurrentRequest = true;
        snap.charge.currentRequest = requestAmps;
        snap.charge.hasActualCurrent = true;
        snap.charge.actualCurrent = actualAmps;
    }
    void asleep() {
        snap.hasReading = true;
        snap.readingAgeSeconds = 0;
        snap.sleep = 2;
        snap.charge = tesla::ChargeState();   // not valid: no charge data while it sleeps
    }
    void finish(bool ok, const std::string& line = "") {
        snap.busy = false;
        snap.hasResult = true;
        snap.resultOk = ok;
        snap.resultJob = snap.job;
        snap.job = "";
        snap.resultLines = {line};
        snap.resultSerial++;
    }
    std::string last() const { return jobs.empty() ? "" : jobs.back(); }
};

struct Rig {
    FakePlatform platform;
    FakeCar car;
    HistoryService history{platform};
    CarCommander commander{platform, car};
    Rig() {
        platform.epoch = 1760000000;   // a clock that counts as set, or no event is recorded
        commander.setHistory(&history);
        commander.setActive(true);
    }
    void tick(int seconds = 1) {
        platform.now += seconds * 1000ULL;
        platform.epoch += seconds;
        car.snap.readingAgeSeconds += car.snap.hasReading ? seconds : 0;
        commander.loop();
    }
    /** A reading arrives (somebody pressed Check now, or a check finished): the car is in this state. */
    void observe(int code, int request, int actual, int sleep = 1) {
        car.setCar(code, request, actual, sleep);
        car.snap.resultOk = true;
        car.snap.resultSerial++;
        commander.loop();
    }
    void observeAsleep() {
        car.asleep();
        car.snap.resultOk = true;
        car.snap.resultSerial++;
        commander.loop();
    }
    /** Runs the job the commander just started to a successful end, with the car in the given state afterwards. */
    void complete(int code, int request, int actual) {
        car.setCar(code, request, actual);
        car.finish(true, "ok");
        commander.loop();
    }
    int events() { return static_cast<int>(history.recentEvents(50).size()); }
    std::string lastEvent() { auto e = history.recentEvents(1); return e.empty() ? "" : e[0].text; }
};

}  // namespace

// ---- what the commander does with the planner's wish ---------------------------------------------------------------------------------

void test_with_no_knowledge_it_looks_at_the_car_first_and_sends_no_command() {
    Rig r;
    r.commander.sendChargingLimit(12);
    r.tick();
    TEST_ASSERT_EQUAL(1, (int)r.car.jobs.size());
    testing::check("read", r.car.last());
    TEST_ASSERT_TRUE(r.car.quietReads);                    // the controller's own reads stay out of the log
}

void test_a_charging_car_is_given_the_new_current() {
    Rig r;
    r.observe(5, 18, 18);
    r.commander.sendChargingLimit(12);
    r.tick();
    testing::check("amps 12", r.car.last());
    r.complete(5, 12, 12);
    r.tick();
    TEST_ASSERT_EQUAL(1, (int)r.car.jobs.size());          // already at 12 A: nothing more to send
}

void test_nothing_is_sent_when_the_car_already_has_that_current() {
    Rig r;
    r.observe(5, 12, 12);
    r.commander.sendChargingLimit(12);
    r.tick();
    TEST_ASSERT_EQUAL(0, (int)r.car.jobs.size());
}

void test_a_stop_request_stops_a_charging_car_and_leaves_a_stopped_one_alone() {
    Rig r;
    r.observe(5, 18, 18);
    r.commander.sendChargingLimit(0);
    r.tick();
    testing::check("stop", r.car.last());
    r.complete(7, 18, 0);
    r.tick();
    TEST_ASSERT_EQUAL(1, (int)r.car.jobs.size());          // stopped already: nothing more
    r.commander.sendChargingLimit(0);
    r.tick();
    TEST_ASSERT_EQUAL(1, (int)r.car.jobs.size());
}

void test_starting_a_stopped_car_sets_the_current_first_and_then_starts() {
    Rig r;
    r.observe(7, 16, 0);
    r.commander.sendChargingLimit(10);
    r.tick();
    testing::check("amps 10", r.car.last());
    r.complete(7, 10, 0);                                   // the read-back after the command
    r.tick();
    testing::check("start", r.car.last());
    r.complete(5, 10, 10);
    r.tick();
    TEST_ASSERT_EQUAL(2, (int)r.car.jobs.size());
}

void test_a_stopped_car_that_already_has_the_current_is_just_started() {
    Rig r;
    r.observe(7, 10, 0);
    r.commander.sendChargingLimit(10);
    r.tick();
    testing::check("start", r.car.last());
}

void test_an_unplugged_or_complete_car_is_left_alone() {
    for (int code : {2, 6, 3}) {
        Rig r;
        r.observe(code, 16, 0);
        r.commander.sendChargingLimit(10);
        r.tick();
        TEST_ASSERT_EQUAL_MESSAGE(0, (int)r.car.jobs.size(), "disconnected, complete or no power: do not start");
    }
}

void test_a_sleeping_car_is_not_commanded_and_the_state_says_so() {
    Rig r;
    r.observeAsleep();
    r.commander.sendChargingLimit(10);
    r.tick();
    TEST_ASSERT_EQUAL(0, (int)r.car.jobs.size());
    TEST_ASSERT_TRUE(r.commander.status().state.find("asleep") != std::string::npos);
    TEST_ASSERT_TRUE(r.commander.status().state.find("waking is off") != std::string::npos);
}

void test_a_fixed_current_is_kept_in_force_and_beats_the_planner() {
    Rig r;
    r.observe(5, 18, 18);
    r.commander.holdFixedLimit(0, "paused by you");        // the kill switch
    r.commander.sendChargingLimit(16);                     // the planner still asks for 16
    r.tick();
    testing::check("stop", r.car.last());
    r.complete(7, 18, 0);
    r.commander.holdFixedLimit(std::nullopt);
    r.commander.sendChargingLimit(16);
    r.tick();
    testing::check("amps 16", r.car.last());
}

void test_it_trusts_what_it_set_and_only_checks_the_car_every_ten_minutes() {
    Rig r;
    r.observe(5, 18, 18);
    r.commander.sendChargingLimit(18);
    for (int s = 0; s < 590; s++) r.tick();
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)r.car.jobs.size(), "no routine reads in between");
    for (int s = 0; s < 20; s++) r.tick();
    TEST_ASSERT_EQUAL(1, (int)r.car.jobs.size());
    testing::check("read", r.car.last());                  // the ten-minute check
    r.complete(5, 18, 18);
    for (int s = 0; s < 590; s++) r.tick();
    TEST_ASSERT_EQUAL(1, (int)r.car.jobs.size());          // and the clock starts over
}

void test_the_check_finds_a_change_made_by_someone_else_and_puts_it_right() {
    Rig r;
    r.observe(5, 18, 18);
    r.commander.sendChargingLimit(18);
    for (int s = 0; s < 605; s++) r.tick();                // the check starts
    r.complete(5, 10, 10);                                 // somebody lowered it to 10 A on the car
    r.tick();
    testing::check("amps 18", r.car.last());               // WattSync's value is set again
}

void test_the_check_notices_a_car_that_stopped_by_itself() {
    Rig r;
    r.observe(5, 18, 18);
    r.commander.sendChargingLimit(18);
    for (int s = 0; s < 605; s++) r.tick();
    r.complete(6, 18, 0);                                  // it reached its charge limit
    TEST_ASSERT_TRUE(*r.commander.primaryStatus() == ChargerStatus::Finishing);
    TEST_ASSERT_EQUAL(0, (int)r.commander.currentDrawAmps());
    r.tick();
    TEST_ASSERT_EQUAL(1, (int)r.car.jobs.size());          // and it is not restarted
}

void test_the_commander_waits_while_a_job_is_running() {
    Rig r;
    r.observe(5, 18, 18);
    r.commander.sendChargingLimit(12);
    r.tick();
    r.commander.sendChargingLimit(10);                     // a new wish arrives mid-job
    r.tick(); r.tick();
    TEST_ASSERT_EQUAL(1, (int)r.car.jobs.size());
    r.complete(5, 12, 12);
    r.tick();
    testing::check("amps 10", r.car.last());               // the latest wish wins
}

// ---- what the planner reads back ----------------------------------------------------------------------------------------------------

void test_the_planner_is_told_what_wattsync_set_not_a_measurement() {
    Rig r;
    TEST_ASSERT_FALSE(r.commander.primaryStatus().has_value());      // never looked
    TEST_ASSERT_FALSE(r.commander.currentLimitAmps().has_value());
    r.observe(5, 14, 13);                                            // the car says 14 A set, 13 A drawn
    TEST_ASSERT_TRUE(*r.commander.primaryStatus() == ChargerStatus::Charging);
    TEST_ASSERT_EQUAL(14, *r.commander.currentLimitAmps());
    TEST_ASSERT_EQUAL(14, (int)r.commander.currentDrawAmps());       // what was set: WattSync does not measure the car
    TEST_ASSERT_FALSE(r.commander.drawIsMeasured());
    r.observe(7, 14, 0);
    TEST_ASSERT_TRUE(*r.commander.primaryStatus() == ChargerStatus::SuspendedEV);
    TEST_ASSERT_FALSE(r.commander.currentLimitAmps().has_value());   // not charging: no limit in force
    TEST_ASSERT_EQUAL(0, (int)r.commander.currentDrawAmps());
    r.observe(2, 14, 0);
    TEST_ASSERT_TRUE(*r.commander.primaryStatus() == ChargerStatus::Available);
    r.observe(6, 14, 0);
    TEST_ASSERT_TRUE(*r.commander.primaryStatus() == ChargerStatus::Finishing);
}

void test_after_its_own_command_the_numbers_follow_without_another_read() {
    Rig r;
    r.observe(5, 18, 18);
    r.commander.sendChargingLimit(12);
    r.tick();
    r.car.snap.readingAgeSeconds = 100;                    // the read-back failed: only the command went through
    r.car.finish(true, "Charging current set to 12 A.");
    r.commander.loop();
    TEST_ASSERT_EQUAL(12, *r.commander.currentLimitAmps());
    TEST_ASSERT_EQUAL(12, (int)r.commander.currentDrawAmps());
    r.tick();
    TEST_ASSERT_EQUAL(1, (int)r.car.jobs.size());          // no extra read, no repeated command
    r.commander.sendChargingLimit(0);
    r.tick();
    testing::check("stop", r.car.last());
    r.car.snap.readingAgeSeconds = 100;
    r.car.finish(true, "Charging stopped.");
    r.commander.loop();
    TEST_ASSERT_EQUAL(0, (int)r.commander.currentDrawAmps());
    TEST_ASSERT_TRUE(*r.commander.primaryStatus() == ChargerStatus::SuspendedEV);
}

void test_a_car_that_falls_asleep_keeps_its_last_known_plugged_in_state() {
    Rig r;
    r.observe(7, 14, 0);
    r.observeAsleep();
    TEST_ASSERT_TRUE(*r.commander.primaryStatus() == ChargerStatus::SuspendedEV);
    TEST_ASSERT_EQUAL(0, (int)r.commander.currentDrawAmps());
}

void test_commands_are_recorded_in_the_activity_log_with_their_reason() {
    Rig r;
    r.observe(5, 18, 18);
    r.commander.setCommandContext("less solar left");
    r.commander.sendChargingLimit(12);
    r.commander.setCommandContext("");
    r.tick();
    r.complete(5, 12, 12);
    r.tick();
    testing::check("Car: changed limit to 12 A: less solar left", r.lastEvent());
    testing::check("Car: changed limit to 12 A", r.commander.lastCommand());
    TEST_ASSERT_TRUE(r.commander.lastCommandAtSeconds() > 0);
}

void test_the_status_says_what_wattsync_set() {
    Rig r;
    r.observe(5, 16, 16);
    TEST_ASSERT_TRUE(r.commander.status().state.find("Charging at 16 A, as set by WattSync") != std::string::npos);
    r.observe(2, 16, 0);
    TEST_ASSERT_TRUE(r.commander.status().state.find("not plugged in") != std::string::npos);
}

// ---- waking a sleeping car ------------------------------------------------------------------------------------------------------------

void test_a_sleeping_car_is_woken_only_when_the_owner_allowed_it_and_charging_is_wanted() {
    Rig r;
    r.observe(7, 16, 0);
    r.observeAsleep();
    r.commander.sendChargingLimit(10);
    r.tick();
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)r.car.jobs.size(), "waking is off by default");
    r.commander.setAllowWake(true);
    r.commander.sendChargingLimit(0);
    r.tick();
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)r.car.jobs.size(), "nothing to charge, so no reason to wake it");
    r.commander.sendChargingLimit(10);
    r.tick();
    testing::check("wake", r.car.last());
    TEST_ASSERT_TRUE(r.lastEvent().find("waking the car") != std::string::npos);
}

void test_after_a_wake_the_car_is_looked_at_at_once_and_then_commanded() {
    Rig r;
    r.commander.setAllowWake(true);
    r.observe(7, 16, 0);
    r.observeAsleep();
    r.commander.sendChargingLimit(10);
    r.tick();
    testing::check("wake", r.car.last());
    r.car.snap.hasReading = true;
    r.car.snap.readingAgeSeconds = 100;
    r.car.finish(true, "The car is awake.");
    r.commander.loop();
    r.tick();
    testing::check("read", r.car.last());                  // a look at the awake car
    r.complete(7, 16, 0);
    r.tick();
    testing::check("amps 10", r.car.last());
}

void test_the_car_is_woken_at_most_once_every_thirty_minutes() {
    Rig r;
    r.commander.setAllowWake(true);
    r.observe(7, 16, 0);
    r.observeAsleep();
    r.commander.sendChargingLimit(10);
    r.tick();
    testing::check("wake", r.car.last());
    r.car.finish(false, "The car did not wake. It may take another try.");
    r.commander.loop();
    TEST_ASSERT_EQUAL(0, r.commander.status().unreachableSeconds);   // heard but sleepy: not an outage
    size_t wakes = 0;
    for (int s = 0; s < 1700; s++) {
        r.tick();
        if (r.car.snap.busy) {                                      // the ten-minute checks: the car is still asleep
            r.car.asleep();
            r.car.snap.hasResult = true;
            r.car.finish(true, "asleep");
            r.commander.loop();
        }
    }
    for (const auto& j : r.car.jobs) wakes += j == "wake";
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)wakes, "still inside the thirty minutes");
    for (int s = 0; s < 200; s++) {
        r.tick();
        if (r.car.snap.busy) { r.car.asleep(); r.car.finish(true, "asleep"); r.commander.loop(); }
    }
    wakes = 0;
    for (const auto& j : r.car.jobs) wakes += j == "wake";
    TEST_ASSERT_EQUAL(2, (int)wakes);
}

void test_a_car_known_to_be_unplugged_or_full_or_never_seen_awake_is_not_woken() {
    for (int code : {2, 6}) {
        Rig r;
        r.commander.setAllowWake(true);
        r.observe(code, 16, 0);
        r.observeAsleep();
        r.commander.sendChargingLimit(10);
        r.tick();
        TEST_ASSERT_EQUAL(0, (int)r.car.jobs.size());
    }
    Rig r;
    r.commander.setAllowWake(true);
    r.observeAsleep();                                      // first look: asleep, plug state unknown
    r.commander.sendChargingLimit(10);
    r.tick();
    TEST_ASSERT_EQUAL(0, (int)r.car.jobs.size());
}

void test_a_sleeping_car_status_says_whether_waking_is_on() {
    Rig r;
    r.observeAsleep();
    TEST_ASSERT_TRUE(r.commander.status().state.find("waking is off") != std::string::npos);
    r.commander.setAllowWake(true);
    TEST_ASSERT_TRUE(r.commander.status().state.find("will wake it when charging is wanted") != std::string::npos);
    TEST_ASSERT_EQUAL(-1, r.commander.status().lastWakeAgeSeconds);
}

// ---- a car that cannot be reached -----------------------------------------------------------------------------------------------------

/** Seconds from now until the commander starts its next job (it must start one within 1000 s). */
static int secondsUntilNextJob(Rig& r) {
    size_t before = r.car.jobs.size();
    for (int s = 1; s <= 1000; s++) {
        r.tick();
        if (r.car.jobs.size() > before) return s;
    }
    return -1;
}

void test_failed_jobs_are_retried_after_30_60_120_and_then_300_seconds() {
    Rig r;
    r.commander.sendChargingLimit(10);
    TEST_ASSERT_EQUAL(1, secondsUntilNextJob(r));            // the first read starts at once
    int expected[] = {30, 60, 120, 300, 300};
    for (int wait : expected) {
        r.car.finish(false, "The car was not heard. Move the controller closer to the car and try again.");
        r.commander.loop();
        TEST_ASSERT_EQUAL(wait, secondsUntilNextJob(r));
    }
}

void test_a_success_after_failures_starts_the_backoff_over() {
    Rig r;
    r.commander.sendChargingLimit(10);
    r.tick();
    r.car.finish(false, "not heard");
    r.commander.loop();
    for (int s = 0; s < 31; s++) r.tick();
    TEST_ASSERT_EQUAL(2, (int)r.car.jobs.size());
    r.complete(5, 10, 10);                                 // the car answered this time
    r.car.snap.charge.currentRequest = 10;
    r.tick();
    TEST_ASSERT_EQUAL(0, r.commander.status().unreachableSeconds);
    r.commander.sendChargingLimit(8);
    r.tick();
    testing::check("amps 8", r.car.last());                // no left-over waiting
    r.car.finish(false, "not heard");
    r.commander.loop();
    for (int s = 0; s < 29; s++) r.tick();
    size_t n = r.car.jobs.size();
    r.tick(); r.tick();
    TEST_ASSERT_EQUAL(n + 1, r.car.jobs.size());           // retried after 30 s again, not 60
}

void test_keep_last_setting_is_silent_about_an_outage() {
    Rig r;
    r.commander.setOnReturn(CarOnReturn::Keep);
    r.commander.sendChargingLimit(10);
    r.tick();
    r.car.finish(false, "not heard");
    r.commander.loop();
    for (int s = 0; s < 700; s++) r.tick();
    TEST_ASSERT_EQUAL(0, r.events());
    TEST_ASSERT_TRUE(r.commander.status().unreachableSeconds >= 600);
}

void test_just_tell_me_reports_after_ten_minutes_and_when_the_car_is_back() {
    Rig r;
    r.commander.setOnReturn(CarOnReturn::Tell);
    r.car.setCar(5, 18, 18);
    r.car.snap.readingAgeSeconds = 400;                    // forces a read
    r.commander.sendChargingLimit(10);
    r.tick();
    r.car.finish(false, "not heard");
    r.commander.loop();
    for (int s = 0; s < 590; s++) r.tick();
    TEST_ASSERT_EQUAL(0, r.events());                      // not yet 10 minutes
    for (int s = 0; s < 20; s++) r.tick();
    TEST_ASSERT_EQUAL(1, r.events());
    TEST_ASSERT_TRUE(r.lastEvent().find("Car not reachable for 10 minutes") != std::string::npos);
    for (int s = 0; s < 300; s++) r.tick();                // still out: said once only
    TEST_ASSERT_EQUAL(1, r.events());
    // the car comes back
    while (!r.car.snap.busy) r.tick();
    r.complete(5, 18, 18);
    r.tick();
    TEST_ASSERT_TRUE(r.lastEvent().find("Car reachable again") != std::string::npos);
}

void test_a_short_outage_is_never_reported() {
    Rig r;
    r.commander.setOnReturn(CarOnReturn::Tell);
    r.commander.sendChargingLimit(10);
    r.tick();
    r.car.finish(false, "not heard");
    r.commander.loop();
    for (int s = 0; s < 40; s++) r.tick();
    r.complete(5, 10, 10);
    r.tick();
    TEST_ASSERT_EQUAL(0, r.events());
}

void test_stop_when_it_is_back_stops_the_charge_once_after_a_long_outage() {
    Rig r;
    r.commander.setOnReturn(CarOnReturn::Stop);
    r.commander.sendChargingLimit(16);
    r.tick();
    r.car.finish(false, "not heard");
    r.commander.loop();
    for (int s = 0; s < 650; s++) r.tick();
    TEST_ASSERT_EQUAL(1, r.events());                      // "not reachable for 10 minutes"
    while (!r.car.snap.busy) r.tick();
    r.complete(5, 16, 16);                                 // reachable again: the car is charging at 16 A
    r.commander.sendChargingLimit(16);                     // the planner is happy with 16 A...
    r.tick();
    testing::check("stop", r.car.last());                  // ...but the owner chose to stop on return
    r.complete(7, 16, 0);
    r.tick();
    r.commander.sendChargingLimit(16);                     // then normal planning resumes
    r.tick();
    testing::check("start", r.car.last());                 // the car still has 16 A set, so it only needs starting
    size_t stops = 0;
    for (const auto& j : r.car.jobs) stops += j == "stop";
    TEST_ASSERT_EQUAL(1, (int)stops);
}

void test_stop_when_it_is_back_does_nothing_after_a_short_outage() {
    Rig r;
    r.commander.setOnReturn(CarOnReturn::Stop);
    r.commander.sendChargingLimit(16);
    r.tick();
    r.car.finish(false, "not heard");
    r.commander.loop();
    for (int s = 0; s < 100; s++) r.tick();
    while (!r.car.snap.busy) r.tick();
    r.complete(5, 16, 16);
    r.commander.sendChargingLimit(16);
    r.tick();
    for (const auto& j : r.car.jobs) TEST_ASSERT_TRUE(j != "stop");
}

void test_a_car_that_is_asleep_is_not_an_outage() {
    Rig r;
    r.commander.setOnReturn(CarOnReturn::Tell);
    r.car.setCar(7, 16, 0);
    r.commander.sendChargingLimit(10);
    r.tick();
    r.car.asleep();                                        // what a status read shows while it sleeps
    r.car.finish(false, "The car is asleep, so the command was not sent. Waking it is not done here; wake it from the Tesla app and try again.");
    r.commander.loop();
    for (int s = 0; s < 900; s++) r.tick();
    TEST_ASSERT_EQUAL(0, r.events());
    TEST_ASSERT_EQUAL(0, r.commander.status().unreachableSeconds);
    TEST_ASSERT_TRUE(r.commander.status().state.find("asleep") != std::string::npos);
}

void test_nothing_happens_while_the_car_is_not_in_command() {
    Rig r;
    r.commander.setActive(false);
    r.car.setCar(5, 18, 18);
    r.commander.sendChargingLimit(0);
    r.tick();
    TEST_ASSERT_EQUAL(0, (int)r.car.jobs.size());
    TEST_ASSERT_FALSE(r.commander.hasConnectedChargers());
    TEST_ASSERT_TRUE(r.car.controlled == false);
}

void test_an_unpaired_car_is_not_a_charger() {
    Rig r;
    r.car.snap.paired = false;
    TEST_ASSERT_FALSE(r.commander.hasConnectedChargers());
    r.commander.sendChargingLimit(10);
    r.tick();
    TEST_ASSERT_EQUAL(0, (int)r.car.jobs.size());
}

// ---- the router: one commander at a time ------------------------------------------------------------------------------------------------

namespace {
class RecordingDevice : public ChargeDevice {
public:
    std::vector<std::string> calls;
    bool connected = true;
    std::string cmd = "wallbox command";
    bool hasConnectedChargers() const override { return connected; }
    void sendChargingLimit(int amps) override { calls.push_back("limit " + std::to_string(amps)); }
    void holdFixedLimit(std::optional<int> amps, const std::string&) override { calls.push_back(amps ? "hold " + std::to_string(*amps) : "release"); }
    void setHistory(HistoryService*) override {}
    void setCommandContext(const std::string&) override {}
    void refreshMeasurements() override { calls.push_back("refresh"); }
    double currentDrawAmps() override { return 7; }
    bool drawIsMeasured() override { return true; }
    std::optional<int> currentLimitAmps() override { return 9; }
    bool anyRestored() override { return false; }
    std::optional<ChargerStatus> primaryStatus() override { return ChargerStatus::Charging; }
    const std::string& lastCommand() const override { return cmd; }
    int64_t lastCommandAtSeconds() const override { return 1; }
};

class MemoryCarSettings : public CarSettingsStore {
public:
    bool through = false;
    int onReturn = 0;
    bool allowWake = false;
    bool loadAllowWake() override { return allowWake; }
    void saveAllowWake(bool v) override { allowWake = v; }
    bool loadThrough() override { return through; }
    void saveThrough(bool v) override { through = v; }
    int loadOnReturn() override { return onReturn; }
    void saveOnReturn(int v) override { onReturn = v; }
};
}  // namespace

void test_the_router_sends_everything_to_the_wallbox_until_the_car_is_chosen() {
    FakePlatform platform;
    RecordingDevice wallbox;
    FakeCar car;
    CarCommander commander(platform, car);
    ChargeRouter router(wallbox, commander, car);
    router.sendChargingLimit(12);
    router.refreshMeasurements();
    TEST_ASSERT_EQUAL(2, (int)wallbox.calls.size());
    TEST_ASSERT_EQUAL(7, (int)router.currentDrawAmps());
    TEST_ASSERT_EQUAL(0, (int)car.jobs.size());
    TEST_ASSERT_FALSE(router.throughCar());
}

void test_switching_to_the_car_releases_the_wallbox_and_hands_over_and_back() {
    FakePlatform platform;
    platform.epoch = 1760000000;
    RecordingDevice wallbox;
    FakeCar car;
    CarCommander commander(platform, car);
    HistoryService history(platform);
    MemoryCarSettings settings;
    ChargeRouter router(wallbox, commander, car);
    router.setHistory(&history);
    router.setSettings(&settings);
    int changed = 0;
    router.onChanged([&] { changed++; });

    TEST_ASSERT_TRUE(router.setThroughCar(true).ok);
    TEST_ASSERT_TRUE(router.throughCar());
    TEST_ASSERT_TRUE(settings.through);
    TEST_ASSERT_EQUAL(1, changed);
    TEST_ASSERT_TRUE(car.controlled);                       // the separate watch reads pause
    testing::check("release", wallbox.calls.back());        // any fixed hold on the wallbox is let go
    wallbox.calls.clear();
    router.sendChargingLimit(12);
    router.holdFixedLimit(0, "paused by you");
    TEST_ASSERT_EQUAL(0, (int)wallbox.calls.size());        // the wallbox gets nothing now
    TEST_ASSERT_EQUAL(0, (int)router.currentDrawAmps());    // numbers come from the car (no reading yet)

    TEST_ASSERT_TRUE(router.setThroughCar(false).ok);
    TEST_ASSERT_FALSE(router.throughCar());
    TEST_ASSERT_FALSE(settings.through);
    TEST_ASSERT_EQUAL(2, changed);
    TEST_ASSERT_FALSE(car.controlled);
    router.sendChargingLimit(9);
    testing::check("limit 9", wallbox.calls.back());
    TEST_ASSERT_TRUE(history.recentEvents(5).size() >= 2);
}

void test_the_car_cannot_be_chosen_unless_it_is_paired() {
    FakePlatform platform;
    RecordingDevice wallbox;
    FakeCar car;
    car.snap.paired = false;
    CarCommander commander(platform, car);
    ChargeRouter router(wallbox, commander, car);
    CarResult r = router.setThroughCar(true);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(r.message.find("paired") != std::string::npos);
    TEST_ASSERT_FALSE(router.throughCar());
}

void test_the_choice_and_the_policy_survive_a_restart_but_an_unpaired_car_never_takes_command() {
    FakePlatform platform;
    RecordingDevice wallbox;
    FakeCar car;
    CarCommander commander(platform, car);
    MemoryCarSettings settings;
    settings.through = true;
    settings.onReturn = 2;
    settings.allowWake = true;
    ChargeRouter router(wallbox, commander, car);
    router.setSettings(&settings);
    router.restore();
    TEST_ASSERT_TRUE(router.throughCar());
    TEST_ASSERT_TRUE(commander.onReturn() == CarOnReturn::Stop);
    TEST_ASSERT_TRUE(commander.allowWake());

    ChargeRouter second(wallbox, commander, car);
    car.snap.paired = false;
    second.setSettings(&settings);
    second.restore();
    TEST_ASSERT_FALSE(second.throughCar());
    TEST_ASSERT_FALSE(settings.through);                    // and the saved choice is corrected
}

void test_the_solar_controller_drives_the_car_through_the_router() {
    FakePlatform platform;
    platform.epoch = 1760000000;
    RecordingDevice wallbox;
    FakeCar car;
    CarCommander commander(platform, car);
    ChargeRouter router(wallbox, commander, car);
    struct Store : SettingsStore {
        std::optional<Settings> load() override { return std::nullopt; }
        void save(const Settings&) override {}
    } store;
    struct Always : DaylightSchedule {
        bool isOpen(int64_t) const override { return true; }
        bool windowFor(int64_t, int64_t&, int64_t&) const override { return false; }
        void setMargins(int, int) override {}
        std::string describe(int64_t) const override { return "all day"; }
    } schedule;
    SystemState state;
    SolarController controller(platform, state, router, schedule, store);
    router.onChanged([&] { controller.onCommanderChanged(); });
    TEST_ASSERT_TRUE(router.setThroughCar(true).ok);
    controller.setMode(ChargingMode::Fast);                 // Custom: a fixed current, held
    controller.setFastAmps(14);
    car.setCar(5, 32, 32);
    commander.loop();
    testing::check("amps 14", car.last());                  // the car, not the wallbox, got the fixed current
    for (const auto& c : wallbox.calls) TEST_ASSERT_TRUE(c == "release" || c == "refresh");
}

void test_allowing_the_wake_is_saved_and_off_by_default() {
    FakePlatform platform;
    RecordingDevice wallbox;
    FakeCar car;
    CarCommander commander(platform, car);
    MemoryCarSettings settings;
    ChargeRouter router(wallbox, commander, car);
    router.setSettings(&settings);
    TEST_ASSERT_FALSE(commander.allowWake());
    router.setAllowWake(true);
    TEST_ASSERT_TRUE(commander.allowWake());
    TEST_ASSERT_TRUE(settings.allowWake);
    router.setAllowWake(false);
    TEST_ASSERT_FALSE(settings.allowWake);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_with_no_knowledge_it_looks_at_the_car_first_and_sends_no_command);
    RUN_TEST(test_a_charging_car_is_given_the_new_current);
    RUN_TEST(test_nothing_is_sent_when_the_car_already_has_that_current);
    RUN_TEST(test_a_stop_request_stops_a_charging_car_and_leaves_a_stopped_one_alone);
    RUN_TEST(test_starting_a_stopped_car_sets_the_current_first_and_then_starts);
    RUN_TEST(test_a_stopped_car_that_already_has_the_current_is_just_started);
    RUN_TEST(test_an_unplugged_or_complete_car_is_left_alone);
    RUN_TEST(test_a_sleeping_car_is_not_commanded_and_the_state_says_so);
    RUN_TEST(test_a_fixed_current_is_kept_in_force_and_beats_the_planner);
    RUN_TEST(test_it_trusts_what_it_set_and_only_checks_the_car_every_ten_minutes);
    RUN_TEST(test_the_check_finds_a_change_made_by_someone_else_and_puts_it_right);
    RUN_TEST(test_the_check_notices_a_car_that_stopped_by_itself);
    RUN_TEST(test_the_commander_waits_while_a_job_is_running);
    RUN_TEST(test_the_planner_is_told_what_wattsync_set_not_a_measurement);
    RUN_TEST(test_after_its_own_command_the_numbers_follow_without_another_read);
    RUN_TEST(test_a_car_that_falls_asleep_keeps_its_last_known_plugged_in_state);
    RUN_TEST(test_commands_are_recorded_in_the_activity_log_with_their_reason);
    RUN_TEST(test_the_status_says_what_wattsync_set);
    RUN_TEST(test_a_sleeping_car_is_woken_only_when_the_owner_allowed_it_and_charging_is_wanted);
    RUN_TEST(test_after_a_wake_the_car_is_looked_at_at_once_and_then_commanded);
    RUN_TEST(test_the_car_is_woken_at_most_once_every_thirty_minutes);
    RUN_TEST(test_a_car_known_to_be_unplugged_or_full_or_never_seen_awake_is_not_woken);
    RUN_TEST(test_a_sleeping_car_status_says_whether_waking_is_on);
    RUN_TEST(test_failed_jobs_are_retried_after_30_60_120_and_then_300_seconds);
    RUN_TEST(test_a_success_after_failures_starts_the_backoff_over);
    RUN_TEST(test_keep_last_setting_is_silent_about_an_outage);
    RUN_TEST(test_just_tell_me_reports_after_ten_minutes_and_when_the_car_is_back);
    RUN_TEST(test_a_short_outage_is_never_reported);
    RUN_TEST(test_stop_when_it_is_back_stops_the_charge_once_after_a_long_outage);
    RUN_TEST(test_stop_when_it_is_back_does_nothing_after_a_short_outage);
    RUN_TEST(test_a_car_that_is_asleep_is_not_an_outage);
    RUN_TEST(test_nothing_happens_while_the_car_is_not_in_command);
    RUN_TEST(test_an_unpaired_car_is_not_a_charger);
    RUN_TEST(test_the_router_sends_everything_to_the_wallbox_until_the_car_is_chosen);
    RUN_TEST(test_switching_to_the_car_releases_the_wallbox_and_hands_over_and_back);
    RUN_TEST(test_allowing_the_wake_is_saved_and_off_by_default);
    RUN_TEST(test_the_car_cannot_be_chosen_unless_it_is_paired);
    RUN_TEST(test_the_choice_and_the_policy_survive_a_restart_but_an_unpaired_car_never_takes_command);
    RUN_TEST(test_the_solar_controller_drives_the_car_through_the_router);
    return UNITY_END();
}
