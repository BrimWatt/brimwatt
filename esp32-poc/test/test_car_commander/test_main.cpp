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
        snap.job = what.rfind("read", 0) == 0 ? "status" : "charge";
        return r;
    }
    CarResult startPair(const std::string&) override { return begin("pair"); }
    CarResult startRefresh() override { return begin("read"); }
    CarResult startQuietRefresh() override { quietReads = true; return begin("read"); }
    CarResult startStart() override { return begin("start"); }
    CarResult startStop() override { return begin("stop"); }
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

void test_with_no_reading_it_reads_the_car_first_and_sends_no_command() {
    Rig r;
    r.commander.sendChargingLimit(12);
    r.tick();
    TEST_ASSERT_EQUAL(1, (int)r.car.jobs.size());
    testing::check("read", r.car.last());
    TEST_ASSERT_TRUE(r.car.quietReads);                    // the controller's own reads stay out of the log
}

void test_a_charging_car_is_given_the_new_current() {
    Rig r;
    r.car.setCar(5, 18, 18);
    r.commander.sendChargingLimit(12);
    r.tick();
    testing::check("amps 12", r.car.last());
    r.complete(5, 12, 12);
    r.tick();
    TEST_ASSERT_EQUAL(1, (int)r.car.jobs.size());          // already at 12 A: nothing more to send
}

void test_nothing_is_sent_when_the_car_already_has_that_current() {
    Rig r;
    r.car.setCar(5, 12, 12);
    r.commander.sendChargingLimit(12);
    r.tick();
    TEST_ASSERT_EQUAL(0, (int)r.car.jobs.size());
}

void test_a_stop_request_stops_a_charging_car_and_leaves_a_stopped_one_alone() {
    Rig r;
    r.car.setCar(5, 18, 18);
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
    r.car.setCar(7, 16, 0);
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
    r.car.setCar(7, 10, 0);
    r.commander.sendChargingLimit(10);
    r.tick();
    testing::check("start", r.car.last());
}

void test_an_unplugged_or_complete_car_is_left_alone() {
    for (int code : {2, 6, 3}) {
        Rig r;
        r.car.setCar(code, 16, 0);
        r.commander.sendChargingLimit(10);
        r.tick();
        TEST_ASSERT_EQUAL_MESSAGE(0, (int)r.car.jobs.size(), "disconnected, complete or no power: do not start");
    }
}

void test_a_sleeping_car_is_not_commanded_and_the_state_says_so() {
    Rig r;
    r.car.asleep();
    r.commander.sendChargingLimit(10);
    r.tick();
    TEST_ASSERT_EQUAL(0, (int)r.car.jobs.size());
    TEST_ASSERT_TRUE(r.commander.status().state.find("asleep") != std::string::npos);
}

void test_a_fixed_current_is_kept_in_force_and_beats_the_planner() {
    Rig r;
    r.car.setCar(5, 18, 18);
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

void test_a_stale_reading_is_read_again_before_anything_is_decided() {
    Rig r;
    r.car.setCar(5, 18, 18);
    r.car.snap.readingAgeSeconds = 400;
    r.commander.sendChargingLimit(12);
    r.tick();
    testing::check("read", r.car.last());
    TEST_ASSERT_EQUAL(1, (int)r.car.jobs.size());
}

void test_the_commander_waits_while_a_job_is_running() {
    Rig r;
    r.car.setCar(5, 18, 18);
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

void test_the_planner_sees_the_cars_current_limit_and_status() {
    Rig r;
    TEST_ASSERT_FALSE(r.commander.primaryStatus().has_value());      // never read
    TEST_ASSERT_FALSE(r.commander.currentLimitAmps().has_value());
    r.car.setCar(5, 14, 13);
    TEST_ASSERT_TRUE(*r.commander.primaryStatus() == ChargerStatus::Charging);
    TEST_ASSERT_EQUAL(14, *r.commander.currentLimitAmps());
    TEST_ASSERT_EQUAL(13, (int)r.commander.currentDrawAmps());
    TEST_ASSERT_TRUE(r.commander.drawIsMeasured());
    r.car.setCar(7, 14, 0);
    TEST_ASSERT_TRUE(*r.commander.primaryStatus() == ChargerStatus::SuspendedEV);
    TEST_ASSERT_FALSE(r.commander.currentLimitAmps().has_value());   // not charging: no limit in force
    TEST_ASSERT_EQUAL(0, (int)r.commander.currentDrawAmps());
    r.car.setCar(2, 14, 0);
    TEST_ASSERT_TRUE(*r.commander.primaryStatus() == ChargerStatus::Available);
    r.car.setCar(6, 14, 0);
    TEST_ASSERT_TRUE(*r.commander.primaryStatus() == ChargerStatus::Finishing);
}

void test_a_stale_reading_is_not_trusted_as_a_measurement() {
    Rig r;
    r.car.setCar(5, 14, 13);
    r.car.snap.readingAgeSeconds = 400;
    TEST_ASSERT_FALSE(r.commander.drawIsMeasured());
    TEST_ASSERT_EQUAL(0, (int)r.commander.currentDrawAmps());
}

void test_a_car_that_falls_asleep_keeps_its_last_known_plugged_in_state() {
    Rig r;
    r.car.setCar(7, 14, 0);
    (void)r.commander.primaryStatus();
    r.car.asleep();
    TEST_ASSERT_TRUE(*r.commander.primaryStatus() == ChargerStatus::SuspendedEV);
}

void test_commands_are_recorded_in_the_activity_log_with_their_reason() {
    Rig r;
    r.car.setCar(5, 18, 18);
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
    ChargeRouter router(wallbox, commander, car);
    router.setSettings(&settings);
    router.restore();
    TEST_ASSERT_TRUE(router.throughCar());
    TEST_ASSERT_TRUE(commander.onReturn() == CarOnReturn::Stop);

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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_with_no_reading_it_reads_the_car_first_and_sends_no_command);
    RUN_TEST(test_a_charging_car_is_given_the_new_current);
    RUN_TEST(test_nothing_is_sent_when_the_car_already_has_that_current);
    RUN_TEST(test_a_stop_request_stops_a_charging_car_and_leaves_a_stopped_one_alone);
    RUN_TEST(test_starting_a_stopped_car_sets_the_current_first_and_then_starts);
    RUN_TEST(test_a_stopped_car_that_already_has_the_current_is_just_started);
    RUN_TEST(test_an_unplugged_or_complete_car_is_left_alone);
    RUN_TEST(test_a_sleeping_car_is_not_commanded_and_the_state_says_so);
    RUN_TEST(test_a_fixed_current_is_kept_in_force_and_beats_the_planner);
    RUN_TEST(test_a_stale_reading_is_read_again_before_anything_is_decided);
    RUN_TEST(test_the_commander_waits_while_a_job_is_running);
    RUN_TEST(test_the_planner_sees_the_cars_current_limit_and_status);
    RUN_TEST(test_a_stale_reading_is_not_trusted_as_a_measurement);
    RUN_TEST(test_a_car_that_falls_asleep_keeps_its_last_known_plugged_in_state);
    RUN_TEST(test_commands_are_recorded_in_the_activity_log_with_their_reason);
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
    RUN_TEST(test_the_car_cannot_be_chosen_unless_it_is_paired);
    RUN_TEST(test_the_choice_and_the_policy_survive_a_restart_but_an_unpaired_car_never_takes_command);
    RUN_TEST(test_the_solar_controller_drives_the_car_through_the_router);
    return UNITY_END();
}
