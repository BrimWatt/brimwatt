// Host-side tests for the charging planner: smoothing, start and stop rules, ECO and Max solar.
#include "../support/TestSupport.h"

#include "planner/ChargingPlanner.h"

using namespace solar;

namespace {

/** Feeds readings one minute apart, like the real poll. */
struct Feed {
    ChargingPlanner planner;
    uint64_t now = 1000000;

    explicit Feed(ChargingPolicy policy) : planner(policy) {}

    ChargingPlanner::Decision tick(double availableAmps) {
        now += 60000;
        return planner.next(now, availableAmps);
    }

    /** Max solar feeds the solar output alone, with how much the grid may add on top. */
    std::vector<int> runSolar(double solarAmps, double topUpAmps, int ticks) {
        std::vector<int> amps;
        for (int i = 0; i < ticks; i++) {
            now += 60000;
            amps.push_back(planner.next(now, solarAmps, topUpAmps).amps);
        }
        return amps;
    }

    std::vector<int> run(double availableAmps, int ticks) {
        std::vector<int> amps;
        for (int i = 0; i < ticks; i++) amps.push_back(tick(availableAmps).amps);
        return amps;
    }
};

bool allZero(const std::vector<int>& values) {
    for (int v : values) if (v != 0) return false;
    return true;
}

// ---- ECO ----

void test_eco_waits_while_there_is_no_surplus() {
    Feed f(ChargingPolicy::eco());
    auto d = f.tick(3);
    TEST_ASSERT_EQUAL(0, d.amps);
    testing::check("waiting for surplus", d.reason);
    TEST_ASSERT_TRUE(allZero(f.run(3, 10)));
}

void test_eco_starts_only_after_the_surplus_has_lasted_three_minutes() {
    Feed f(ChargingPolicy::eco());
    auto amps = f.run(15, 4);
    TEST_ASSERT_EQUAL(0, amps[0]);
    TEST_ASSERT_EQUAL(0, amps[1]);
    TEST_ASSERT_EQUAL(0, amps[2]);
    TEST_ASSERT_EQUAL(15, amps[3]);
}

void test_a_single_spike_never_starts_a_charge() {
    Feed f(ChargingPolicy::eco());
    f.run(0, 3);
    TEST_ASSERT_EQUAL(0, f.tick(25).amps);
    TEST_ASSERT_TRUE(allZero(f.run(0, 10)));
}

void test_eco_runs_at_the_minimum_through_a_dip_and_stops_after_five_minutes() {
    Feed f(ChargingPolicy::eco());
    f.run(15, 6);                                   // charging
    int stoppedAt = -1;
    for (int i = 1; i <= 12 && stoppedAt < 0; i++) {
        auto d = f.tick(1);                         // 1 A is less than the 6 A minimum
        if (d.amps == 0) {
            stoppedAt = i;
            TEST_ASSERT_TRUE(testing::contains(d.reason, "stop:"));
        } else {
            TEST_ASSERT_TRUE(d.amps >= 6);          // never below the charger's minimum while it waits
        }
    }
    TEST_ASSERT_TRUE(stoppedAt >= 6);               // not before about five minutes of shortfall
    TEST_ASSERT_TRUE(stoppedAt <= 9);               // but soon after
    TEST_ASSERT_TRUE(allZero(f.run(1, 5)));         // and it stays off
}

void test_a_dip_shorter_than_five_minutes_never_stops_the_charge() {
    Feed f(ChargingPolicy::eco());
    f.run(15, 6);
    f.run(1, 4);                                    // about three minutes short
    for (int a : f.run(15, 10)) TEST_ASSERT_TRUE(a >= 6);   // the sun is back: still charging
    for (int a : f.run(1, 4)) TEST_ASSERT_TRUE(a >= 6);     // and the five minutes start over
}

void test_eco_never_holds_the_minimum_on_the_grid() {
    Feed f(ChargingPolicy::eco());
    f.run(15, 6);
    auto weak = f.run(3, 15);                       // 3 A: under the minimum
    bool stopped = false;
    for (int a : weak) {
        if (a == 0) stopped = true;
        if (stopped) TEST_ASSERT_EQUAL(0, a);
    }
    TEST_ASSERT_TRUE(stopped);
}

void test_eco_keeps_charging_while_the_minimum_is_covered() {
    Feed f(ChargingPolicy::eco());
    f.run(15, 6);
    for (int a : f.run(7, 15)) TEST_ASSERT_TRUE(a >= 6);
}

void test_eco_does_not_restart_during_the_minimum_off_time() {
    Feed f(ChargingPolicy::eco());
    f.run(15, 6);
    for (int i = 0; i < 10; i++) if (f.tick(0.5).amps == 0) break;   // stopped just now
    TEST_ASSERT_TRUE(allZero(f.run(15, 1)));        // one minute of sun: still within the 2 minute off time
    auto later = f.run(15, 10);
    TEST_ASSERT_TRUE(later.back() >= 6);            // and then it starts again
}

void test_the_start_confirmation_counts_down_and_no_surplus_has_no_timer() {
    Feed none(ChargingPolicy::eco());
    TEST_ASSERT_EQUAL_INT(-1, none.tick(0).waitSeconds);    // no surplus: nothing to count
    Feed f(ChargingPolicy::eco());
    TEST_ASSERT_EQUAL_INT(180, f.tick(15).waitSeconds);     // the 3 minute confirmation has just begun
    TEST_ASSERT_EQUAL_INT(120, f.tick(15).waitSeconds);
}

void test_the_dip_and_the_pause_after_a_stop_count_down() {
    Feed f(ChargingPolicy::eco());
    f.run(15, 6);                                           // charging
    int previous = -1;
    bool stopped = false;
    for (int i = 0; i < 12 && !stopped; i++) {
        auto d = f.tick(1);
        if (d.amps == 0) { stopped = true; break; }
        if (testing::contains(d.reason, "dip")) {
            TEST_ASSERT_EQUAL_INT(previous > 0 ? previous - 60 : 300, d.waitSeconds);
            previous = d.waitSeconds;
        }
    }
    TEST_ASSERT_TRUE(stopped);
    TEST_ASSERT_EQUAL_INT(60, f.tick(0.5).waitSeconds);     // one minute of the 2 minute pause is left
}

// ---- already-running charges ----

void test_a_charge_found_running_is_adopted_not_stopped() {
    Feed f(ChargingPolicy::eco());
    f.planner.reset(f.now, std::nullopt);
    f.planner.adoptRunning(f.now, 14);
    auto d = f.tick(14);
    TEST_ASSERT_EQUAL(14, d.amps);
}

void test_a_reset_while_charging_keeps_the_limit_until_readings_justify_a_change() {
    Feed f(ChargingPolicy::eco());
    f.planner.reset(f.now, 32);                     // arriving from FAST at 32 A
    TEST_ASSERT_EQUAL(32, f.tick(32).amps);
    int last = 32;
    for (int i = 0; i < 4; i++) last = f.tick(10).amps;
    TEST_ASSERT_TRUE(last < 32);
}

// ---- hysteresis ----

void test_small_increases_inside_the_deadband_are_ignored() {
    Feed f(ChargingPolicy::eco());
    f.run(10, 6);
    for (int a : f.run(11.6, 10)) TEST_ASSERT_EQUAL(10, a);      // 1 A more is not worth a change
    auto up = f.run(13.5, 8);
    TEST_ASSERT_TRUE(up.back() >= 12);                            // 2 A or more goes up
}

void test_it_steps_down_one_amp_when_import_goes_past_the_allowance() {
    Feed f(ChargingPolicy::eco());
    f.run(10, 8);                                                 // charging steadily at 10 A
    TEST_ASSERT_EQUAL(10, f.tick(10).amps);
    // About 9.2 A available: the car draws 10 A and the grid import is above the allowance.
    auto after = f.run(9.2, 4);
    TEST_ASSERT_EQUAL(9, after.back());                           // a 1 A shortfall is enough to step down
}

void test_noisy_sun_causes_far_fewer_changes_than_following_every_reading() {
    Feed f(ChargingPolicy::eco());
    f.run(12, 6);
    uint32_t seed = 12345;
    int changes = 0, naiveChanges = 0, previous = 12, previousNaive = 12;
    for (int i = 0; i < 120; i++) {
        seed = seed * 1664525u + 1013904223u;
        double reading = 12 + ((seed >> 8) % 1000) / 1000.0 * 8 - 4;   // 8 to 16 A
        int amps = f.tick(reading).amps;
        TEST_ASSERT_TRUE(amps >= 6);
        if (amps != previous) changes++;
        previous = amps;
        int naive = static_cast<int>(reading);
        if (naive != previousNaive) naiveChanges++;
        previousNaive = naive;
    }
    // Stepping down at a 1 A gap (to keep import near the allowance) makes more changes than a 2 A dead zone
    // would, but still clearly fewer than following every reading.
    TEST_ASSERT_TRUE_MESSAGE(changes * 3 <= naiveChanges * 2, "planner should change less than a naive follower");
}

void test_the_limit_never_exceeds_the_maximum() {
    Feed f(ChargingPolicy::eco());
    for (int a : f.run(60, 15)) TEST_ASSERT_TRUE(a <= 32);
}

// ---- the installation's maximum current ----

void test_the_maximum_current_caps_both_solar_modes_and_never_raises_their_own_limit() {
    Feed eco(ChargingPolicy::eco());
    eco.planner.setMaxAmps(16);
    eco.run(60, 6);
    for (int a : eco.run(60, 4)) TEST_ASSERT_TRUE(a <= 16);
    TEST_ASSERT_EQUAL(16, eco.run(60, 1).back());
    Feed sun(ChargingPolicy::maxSolar());
    sun.planner.setMaxAmps(12);
    TEST_ASSERT_EQUAL(12, sun.runSolar(30, 2.5, 8).back());
    Feed high(ChargingPolicy::eco());
    high.planner.setMaxAmps(40);                     // the setting's ceiling is above the mode's own 32 A
    TEST_ASSERT_EQUAL(32, high.run(60, 8).back());
}

// ---- MAX SOLAR ----

void test_max_solar_tops_up_from_the_grid_to_reach_the_minimum() {
    Feed f(ChargingPolicy::maxSolar());
    auto amps = f.runSolar(4.2, 2.5, 6);            // 4.2 A of sun, 2.5 A may come from the grid: 6 A is reachable
    TEST_ASSERT_EQUAL(0, amps[0]);
    TEST_ASSERT_EQUAL(0, amps[2]);                  // not before the three minute start delay
    TEST_ASSERT_EQUAL(6, amps[3]);
    TEST_ASSERT_EQUAL(6, amps.back());
}

void test_max_solar_does_not_start_when_the_gap_is_bigger_than_the_allowance() {
    Feed f(ChargingPolicy::maxSolar());
    TEST_ASSERT_TRUE(allZero(f.runSolar(2.0, 2.5, 8)));   // 4 A short of the minimum, only 2.5 A may come from the grid
    TEST_ASSERT_TRUE(allZero(f.runSolar(4.6, 0, 8)));     // no allowance, no top-up
}

void test_max_solar_rounds_up_to_the_next_whole_amp_when_the_gap_fits() {
    Feed up(ChargingPolicy::maxSolar());
    TEST_ASSERT_EQUAL(9, up.runSolar(8.3, 2.5, 8).back());     // 0.7 A from the grid instead of exporting it
    Feed down(ChargingPolicy::maxSolar());
    TEST_ASSERT_EQUAL(8, down.runSolar(8.6, 0, 8).back());     // with no allowance it stays on what the sun covers
}

void test_max_solar_follows_the_sun_and_stops_after_the_dip_delay() {
    Feed f(ChargingPolicy::maxSolar());
    TEST_ASSERT_TRUE(f.runSolar(17, 2.5, 8).back() >= 17);
    for (int a : f.runSolar(0.5, 2.5, 6)) TEST_ASSERT_TRUE(a >= 6);   // rides out a short dip at the minimum
    TEST_ASSERT_EQUAL(0, f.runSolar(0.5, 2.5, 4).back());             // then stops
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_eco_waits_while_there_is_no_surplus);
    RUN_TEST(test_the_start_confirmation_counts_down_and_no_surplus_has_no_timer);
    RUN_TEST(test_the_dip_and_the_pause_after_a_stop_count_down);
    RUN_TEST(test_eco_starts_only_after_the_surplus_has_lasted_three_minutes);
    RUN_TEST(test_a_single_spike_never_starts_a_charge);
    RUN_TEST(test_eco_runs_at_the_minimum_through_a_dip_and_stops_after_five_minutes);
    RUN_TEST(test_a_dip_shorter_than_five_minutes_never_stops_the_charge);
    RUN_TEST(test_eco_never_holds_the_minimum_on_the_grid);
    RUN_TEST(test_eco_keeps_charging_while_the_minimum_is_covered);
    RUN_TEST(test_eco_does_not_restart_during_the_minimum_off_time);
    RUN_TEST(test_a_charge_found_running_is_adopted_not_stopped);
    RUN_TEST(test_a_reset_while_charging_keeps_the_limit_until_readings_justify_a_change);
    RUN_TEST(test_small_increases_inside_the_deadband_are_ignored);
    RUN_TEST(test_it_steps_down_one_amp_when_import_goes_past_the_allowance);
    RUN_TEST(test_noisy_sun_causes_far_fewer_changes_than_following_every_reading);
    RUN_TEST(test_the_limit_never_exceeds_the_maximum);
    RUN_TEST(test_the_maximum_current_caps_both_solar_modes_and_never_raises_their_own_limit);
    RUN_TEST(test_max_solar_tops_up_from_the_grid_to_reach_the_minimum);
    RUN_TEST(test_max_solar_does_not_start_when_the_gap_is_bigger_than_the_allowance);
    RUN_TEST(test_max_solar_rounds_up_to_the_next_whole_amp_when_the_gap_fits);
    RUN_TEST(test_max_solar_follows_the_sun_and_stops_after_the_dip_delay);
    return UNITY_END();
}
