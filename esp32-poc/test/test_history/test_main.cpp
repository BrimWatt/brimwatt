// Host-side tests for the history service: samples, energy totals, the activity log and saving.
#include "../support/TestSupport.h"

#include "common/CivilDate.h"
#include "history/HistoryService.h"
#include "state/RateSchedule.h"

using namespace solar;
using testing::FakePlatform;

namespace {

struct Rig {
    FakePlatform platform;
    HistoryService history{platform};

    Rig() { platform.epoch = daysFromCivil(2026, 10, 14) * 86400 + 12 * 3600 + 7 * 3600; }   // 12:00 local (PDT)

    /** One minute later, record a sample. */
    void minute(double solar, double grid, double car, ChargingMode mode = ChargingMode::Eco, bool paused = false) {
        platform.epoch += 60;
        history.recordSample(solar, grid, car, mode, paused);
    }
    int64_t today() { return history.localDay(platform.epoch); }
};

void test_home_is_what_is_left_after_the_car() {
    Rig r;
    r.minute(3000, 500, 2400);                 // 3000 solar + 500 import - 2400 car = 1100 home
    auto s = r.history.series(0, 60);
    TEST_ASSERT_EQUAL(1, s.size());
    TEST_ASSERT_EQUAL(1100, s[0].home);
    TEST_ASSERT_EQUAL(500, s[0].grid);
    TEST_ASSERT_EQUAL(2400, s[0].car);
    TEST_ASSERT_TRUE(s[0].flags & kFlagCharging);
}

void test_energy_is_counted_between_readings_and_split_into_solar_and_grid() {
    Rig r;
    r.minute(3000, -500, 2000);                // the first reading only starts the clock
    r.minute(3000, -500, 2000);                // 1 minute: 3 kW solar, 2 kW car
    DayTotals t = r.history.totalsForDay(r.today());
    TEST_ASSERT_FLOAT_WITHIN(0.01, 3000.0 / 60, t.solarWh);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 2000.0 / 60, t.carWh);
    // home = 3000 - 500 - 2000 = 500, so 2500 of solar is left and covers the whole car
    TEST_ASSERT_FLOAT_WITHIN(0.01, 2000.0 / 60, t.carSolarWh);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 500.0 / 60, t.exportWh);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0, t.importWh);
}

void test_the_car_part_the_sun_cannot_cover_counts_as_grid() {
    Rig r;
    r.minute(1000, 1900, 2400);                // home = 1000 + 1900 - 2400 = 500; only 500 W of sun is left for the car
    r.minute(1000, 1900, 2400);
    DayTotals t = r.history.totalsForDay(r.today());
    TEST_ASSERT_FLOAT_WITHIN(0.01, 500.0 / 60, t.carSolarWh);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 2400.0 / 60, t.carWh);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 1900.0 / 60, t.importWh);
}

void test_a_long_gap_without_readings_is_not_counted_as_energy() {
    Rig r;
    r.minute(3000, 0, 0);
    r.platform.epoch += 3600;                  // an hour of no readings
    r.history.recordSample(3000, 0, 0, ChargingMode::Eco, false);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0, r.history.totalsForDay(r.today()).solarWh);
}

void test_energy_goes_into_the_local_day_not_the_utc_day() {
    Rig r;
    r.platform.epoch = daysFromCivil(2026, 10, 14) * 86400 + 6 * 3600 + 59 * 60 + 30;   // 23:59:30 local on the 14th
    r.history.recordSample(1000, 0, 0, ChargingMode::Eco, false);
    r.platform.epoch += 60;                                                              // 00:00:30 local on the 15th (07:00 UTC)
    r.history.recordSample(1000, 0, 0, ChargingMode::Eco, false);
    TEST_ASSERT_TRUE(r.history.totalsForDay(daysFromCivil(2026, 10, 15) - 0).solarWh >= 0);
    int64_t next = r.history.localDay(r.platform.epoch);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 1000.0 / 60, r.history.totalsForDay(next).solarWh);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0, r.history.totalsForDay(next - 1).solarWh);
}

void test_the_start_of_the_local_day_is_local_midnight() {
    Rig r;
    int64_t midnight = r.history.startOfLocalDay(r.platform.epoch);
    TEST_ASSERT_EQUAL(daysFromCivil(2026, 10, 14) * 86400 + 7 * 3600, midnight);   // 07:00 UTC is midnight in PDT
}

void test_samples_can_be_averaged_into_buckets() {
    Rig r;
    int64_t from = r.platform.epoch + 60;                                  // the first sample lands exactly here
    for (int i = 0; i < 10; i++) r.minute(1000 * (i < 5 ? 1 : 3), 0, 0);   // 5 minutes of 1 kW then 5 of 3 kW
    auto buckets = r.history.series(from, 300);
    TEST_ASSERT_EQUAL(2, buckets.size());
    TEST_ASSERT_EQUAL(1000, buckets[0].solar);
    TEST_ASSERT_EQUAL(3000, buckets[1].solar);
}

void test_a_bucket_remembers_that_charging_happened_in_it() {
    Rig r;
    int64_t from = r.platform.epoch;
    r.minute(2000, 0, 0);
    r.minute(2000, 0, 2400);
    r.minute(2000, 0, 0);
    auto buckets = r.history.series(from, 300);
    TEST_ASSERT_EQUAL(1, buckets.size());
    TEST_ASSERT_TRUE(buckets[0].flags & kFlagCharging);
}

void test_only_the_last_48_hours_are_kept() {
    Rig r;
    for (size_t i = 0; i < HistoryService::kMaxSamples + 50; i++) r.minute(1000, 0, 0);
    TEST_ASSERT_EQUAL(HistoryService::kMaxSamples, r.history.minuteSampleCount());
    TEST_ASSERT_TRUE(r.history.longRecordCount() > 500);   // the older minutes live on as 5-minute and hourly records
}

void test_events_come_back_newest_first_and_repeats_are_dropped() {
    Rig r;
    r.history.addEvent("Car plugged in");
    r.platform.epoch += 30;
    r.history.addEvent("Car plugged in");          // the same line within two minutes
    r.platform.epoch += 60;
    r.history.addEvent("Started charging at 10 A");
    auto events = r.history.recentEvents(10);
    TEST_ASSERT_EQUAL(2, events.size());
    testing::check("Started charging at 10 A", events[0].text);
    testing::check("Car plugged in", events[1].text);

    r.platform.epoch += 300;
    r.history.addEvent("Car plugged in");          // fine again after a while
    TEST_ASSERT_EQUAL(3, r.history.recentEvents(10).size());
    TEST_ASSERT_EQUAL(1, r.history.recentEvents(1).size());
}

void test_the_rate_schedule_gives_the_peak_price_only_in_the_peak_hours() {
    TEST_ASSERT_EQUAL(30, RateSchedule::centsAt(30, 0, 16, 21, 17));        // no peak price set
    TEST_ASSERT_EQUAL(30, RateSchedule::centsAt(30, 52, 16, 21, 15));
    TEST_ASSERT_EQUAL(52, RateSchedule::centsAt(30, 52, 16, 21, 16));
    TEST_ASSERT_EQUAL(52, RateSchedule::centsAt(30, 52, 16, 21, 20));
    TEST_ASSERT_EQUAL(30, RateSchedule::centsAt(30, 52, 16, 21, 21));       // the end hour is exclusive
    TEST_ASSERT_EQUAL(52, RateSchedule::centsAt(30, 52, 22, 6, 2));         // a peak past midnight wraps around
    TEST_ASSERT_EQUAL(30, RateSchedule::centsAt(30, 52, 22, 6, 12));
}

void test_a_session_remembers_its_mode_and_whether_it_changed() {
    Rig r;
    r.history.sessionStarted(-1);
    for (int i = 0; i < 5; i++) {
        r.platform.epoch += 60;
        r.history.recordSample(3000, 0, 1000, ChargingMode::MaxSolar, false);
    }
    TEST_ASSERT_EQUAL(1, r.history.openSession()->mode);
    TEST_ASSERT_FALSE(r.history.openSession()->mixed);
    r.platform.epoch += 60;
    r.history.recordSample(3000, 0, 1000, ChargingMode::Eco, false);
    TEST_ASSERT_TRUE(r.history.openSession()->mixed);
    std::vector<std::string> lines;
    r.history.forEachLine([&](const std::string& l) { lines.push_back(l); });
    Rig b;
    b.platform.epoch = r.platform.epoch;
    for (const auto& l : lines) b.history.loadLine(l);
    TEST_ASSERT_EQUAL(1, b.history.openSession()->mode);
    TEST_ASSERT_TRUE(b.history.openSession()->mixed);
}

void test_money_saved_uses_the_price_of_the_hour_and_survives_saving() {
    Rig r;
    r.history.setPriceProvider([](int64_t t) { return t % 7200 < 3600 ? 30.0 : 60.0; });   // alternating hours, just for the test
    r.history.sessionStarted(0);
    double expected = 0;
    for (int i = 0; i < 90; i++) {
        r.platform.epoch += 60;
        r.history.recordSample(5000, -2000, 2000, ChargingMode::Eco, false);   // the sun covers the whole 2 kW
        if (i > 0) expected += 2000.0 / 60 / 1000 * (r.platform.epoch % 7200 < 3600 ? 30.0 : 60.0);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05, expected, r.history.totalsForDay(r.today()).savedCents);
    TEST_ASSERT_FLOAT_WITHIN(0.05, expected, r.history.openSession()->savedCents);

    std::vector<std::string> lines;
    r.history.forEachLine([&](const std::string& l) { lines.push_back(l); });
    Rig b;
    b.platform.epoch = r.platform.epoch;
    for (const auto& l : lines) b.history.loadLine(l);
    TEST_ASSERT_FLOAT_WITHIN(0.05, expected, b.history.totalsForDay(b.today()).savedCents);
    TEST_ASSERT_FLOAT_WITHIN(0.05, expected, b.history.openSession()->savedCents);

    Rig old;                                                                  // files written before the money column
    old.history.loadLine("D\t" + std::to_string(old.today()) + "\t1000.0\t500.0\t400.0\t100.0\t200.0");
    old.history.loadLine("T\t" + std::to_string(old.platform.epoch - 1000) + "\t" + std::to_string(old.platform.epoch - 900) + "\t-1.0\t-1.0\t300.0\t250.0\tRemote");   // (a session from the last 30 days)
    TEST_ASSERT_EQUAL(0, old.history.totalsForDay(old.today()).savedCents);
    TEST_ASSERT_EQUAL(1, old.history.recentSessions(5).size());
    TEST_ASSERT_EQUAL(0, old.history.recentSessions(5)[0].savedCents);
}

void test_going_back_to_an_earlier_setting_is_still_logged() {
    Rig r;
    r.history.addEvent("Mode set to Eco");
    r.platform.epoch += 5;
    r.history.addEvent("Mode set to Custom");
    r.platform.epoch += 5;
    r.history.addEvent("Mode set to Eco");         // the same line as two events ago, seconds later
    auto events = r.history.recentEvents(10);
    TEST_ASSERT_EQUAL(3, events.size());
    testing::check("Mode set to Eco", events[0].text);   // the newest line matches the real state
}

void test_the_event_log_keeps_the_latest_fifty() {
    Rig r;
    for (int i = 0; i < 70; i++) {
        r.platform.epoch += 200;
        r.history.addEvent("event " + std::to_string(i));
    }
    auto events = r.history.recentEvents(100);
    TEST_ASSERT_EQUAL(HistoryService::kMaxEvents, events.size());
    testing::check("event 69", events[0].text);
}

void test_everything_survives_being_saved_and_loaded() {
    Rig a;
    a.minute(3000, -500, 2000);
    a.minute(3100, -400, 2000, ChargingMode::MaxSolar, true);
    a.history.addEvent("Mode set to Balanced");

    std::vector<std::string> lines;
    a.history.forEachLine([&](const std::string& line) { lines.push_back(line); });

    Rig b;
    b.platform.epoch = a.platform.epoch;
    for (const auto& line : lines) b.history.loadLine(line);
    auto samples = b.history.series(0, 60);
    TEST_ASSERT_EQUAL(2, samples.size());
    TEST_ASSERT_EQUAL(3100, samples[1].solar);
    TEST_ASSERT_EQUAL(1, samples[1].mode);
    TEST_ASSERT_TRUE(samples[1].flags & kFlagPaused);
    TEST_ASSERT_EQUAL(1, b.history.recentEvents(5).size());
    testing::check("Mode set to Balanced", b.history.recentEvents(5)[0].text);
    TEST_ASSERT_FLOAT_WITHIN(0.1, a.history.totalsForDay(a.today()).solarWh, b.history.totalsForDay(b.today()).solarWh);
}

// A power cut loses at most a minute: the full save plus the journal written afterwards give everything back.
void test_the_journal_on_top_of_the_full_save_restores_everything() {
    Rig a;
    a.minute(3000, -500, 0);
    a.history.markClean();                              // the full save
    std::vector<std::string> full, journal;
    a.history.forEachLine([&](const std::string& l) { full.push_back(l); });

    a.minute(3100, -400, 2000);
    a.history.sessionStarted(100);
    a.history.addEvent("Started charging");
    a.minute(3200, -300, 2000);
    a.history.forEachJournalLine([&](const std::string& l) { journal.push_back(l); });
    TEST_ASSERT_TRUE(a.history.journalPending());
    TEST_ASSERT_TRUE(journal.size() < 12);              // only what is new, not the whole history

    Rig b;
    b.platform.epoch = a.platform.epoch;
    for (const auto& l : full) b.history.loadLine(l);
    for (const auto& l : journal) b.history.loadLine(l);
    for (const auto& l : journal) b.history.loadLine(l);   // replaying twice must not duplicate anything
    TEST_ASSERT_EQUAL(3, b.history.series(0, 60).size());
    TEST_ASSERT_EQUAL(3200, b.history.series(0, 60)[2].solar);
    TEST_ASSERT_EQUAL(1, b.history.recentEvents(5).size());
    TEST_ASSERT_NOT_NULL(b.history.openSession());
    TEST_ASSERT_FLOAT_WITHIN(0.1, a.history.totalsForDay(a.today()).solarWh, b.history.totalsForDay(b.today()).solarWh);
}

void test_a_session_that_ended_after_the_full_save_is_not_left_open() {
    Rig a;
    a.history.sessionStarted(100);
    a.minute(3000, 0, 2000);
    a.history.markClean();
    std::vector<std::string> full, journal;
    a.history.forEachLine([&](const std::string& l) { full.push_back(l); });
    a.minute(3000, 0, 2000);
    a.history.sessionStopped(900, "Remote");
    a.history.forEachJournalLine([&](const std::string& l) { journal.push_back(l); });

    Rig b;
    b.platform.epoch = a.platform.epoch;
    for (const auto& l : full) b.history.loadLine(l);
    for (const auto& l : journal) b.history.loadLine(l);
    TEST_ASSERT_NULL(b.history.openSession());
    TEST_ASSERT_EQUAL(1, b.history.recentSessions(5).size());
}

void test_loading_garbage_does_no_harm() {
    Rig r;
    for (const char* line : {"", "X\t1", "S\tbroken", "S\t0\t1\t2\t3\t4\t5\t6", "E", "D\t1\t2", "\t\t\t"}) r.history.loadLine(line);
    TEST_ASSERT_EQUAL(0, r.history.series(0, 60).size());
    TEST_ASSERT_EQUAL(0, r.history.recentEvents(5).size());
}

void test_seconds_since_boot_do_not_count_as_a_set_clock() {
    Rig r;
    r.platform.epoch = 42;                       // 42 s after boot: the clock has not been set yet
    r.history.recordSample(1000, 0, 0, ChargingMode::Eco, false);
    r.history.addEvent("too early");
    r.history.sessionStarted(100);
    TEST_ASSERT_EQUAL(0, r.history.series(0, 60).size());
    TEST_ASSERT_EQUAL(0, r.history.recentEvents(5).size());
    TEST_ASSERT_NULL(r.history.openSession());
}

void test_nothing_is_recorded_before_the_clock_is_set() {
    Rig r;
    r.platform.epoch = 0;
    r.history.recordSample(1000, 0, 0, ChargingMode::Eco, false);
    r.history.addEvent("too early");
    TEST_ASSERT_EQUAL(0, r.history.series(0, 60).size());
    TEST_ASSERT_EQUAL(0, r.history.recentEvents(5).size());
}

void test_dirty_tracks_unsaved_changes() {
    Rig r;
    TEST_ASSERT_FALSE(r.history.dirty());
    r.minute(1000, 0, 0);
    TEST_ASSERT_TRUE(r.history.dirty());
    r.history.markClean();
    TEST_ASSERT_FALSE(r.history.dirty());
}

// ---- the coarser tiers ----

void test_minutes_are_rolled_up_into_five_minute_records() {
    Rig r;
    int64_t base = r.platform.epoch / 300 * 300 + 300;      // the start of a 5-minute bucket
    r.platform.epoch = base - 60;
    for (int i = 0; i < 12; i++) r.minute(i < 5 ? 1000 : 3000, 0, 0);   // 12 minutes cover parts of 3 buckets
    std::vector<std::string> lines;
    r.history.forEachLongLine([&](const std::string& l) { lines.push_back(l); }, false);
    int fiveMinute = 0;
    for (const auto& l : lines) fiveMinute += l[0] == 'M';
    TEST_ASSERT_TRUE(fiveMinute >= 2);                      // two complete buckets, the third still filling
}

void test_hours_are_rolled_up_from_the_five_minute_records() {
    Rig r;
    for (int i = 0; i < 200; i++) r.minute(2000, 0, 0);     // a little over three hours
    std::vector<std::string> lines;
    r.history.forEachLongLine([&](const std::string& l) { lines.push_back(l); }, false);
    int hourly = 0;
    for (const auto& l : lines) hourly += l[0] == 'H';
    TEST_ASSERT_TRUE(hourly >= 2);
}

void test_an_old_day_comes_from_the_coarser_tiers() {
    Rig r;
    int64_t from = r.platform.epoch + 60;
    for (int i = 0; i < 3 * 1440; i++) r.minute(i % 2 ? 1000 : 3000, 0, 0);   // three days; minutes older than 48 h are gone
    TEST_ASSERT_EQUAL(HistoryService::kMaxSamples, r.history.minuteSampleCount());
    auto firstDay = r.history.seriesRange(from, from + 86399, 3600);
    TEST_ASSERT_TRUE(firstDay.size() >= 20);                // hourly buckets built from 5-minute records
    for (const auto& s : firstDay) {
        char message[64];
        snprintf(message, sizeof message, "bucket at %u averaged %d W", (unsigned)s.t, (int)s.solar);
        TEST_ASSERT_TRUE_MESSAGE(s.solar >= 1700 && s.solar <= 2300, message);   // the average of 1 kW and 3 kW
    }
}

void test_recent_time_still_uses_the_minute_samples() {
    Rig r;
    for (int i = 0; i < 30; i++) r.minute(1000 + i * 10, 0, 0);
    auto last = r.history.seriesRange(r.platform.epoch - 600, INT64_MAX, 60);
    TEST_ASSERT_EQUAL(11, last.size());
    TEST_ASSERT_EQUAL(1000 + 29 * 10, last.back().solar);
}

void test_only_new_long_records_are_written_when_asked() {
    Rig r;
    for (int i = 0; i < 70; i++) r.minute(1000, 0, 0);
    std::vector<std::string> all, firstNew;
    r.history.forEachLongLine([&](const std::string& l) { all.push_back(l); }, false);
    r.history.forEachLongLine([&](const std::string& l) { firstNew.push_back(l); }, true);
    TEST_ASSERT_EQUAL(all.size(), firstNew.size());          // nothing was saved yet
    TEST_ASSERT_TRUE(r.history.longPending());
    r.history.markLongSaved();
    TEST_ASSERT_FALSE(r.history.longPending());
    for (int i = 0; i < 20; i++) r.minute(1000, 0, 0);
    std::vector<std::string> later;
    r.history.forEachLongLine([&](const std::string& l) { later.push_back(l); }, true);
    TEST_ASSERT_TRUE(later.size() >= 3 && later.size() <= 6);   // about 20 minutes: 4 five-minute records
}

void test_the_long_records_survive_being_saved_and_loaded() {
    Rig a;
    for (int i = 0; i < 400; i++) a.minute(1500, -200, 800);
    std::vector<std::string> lines;
    a.history.forEachLongLine([&](const std::string& l) { lines.push_back(l); }, false);
    Rig b;
    b.platform.epoch = a.platform.epoch;
    for (const auto& l : lines) b.history.loadLine(l);
    TEST_ASSERT_EQUAL(a.history.longRecordCount(), b.history.longRecordCount());
    TEST_ASSERT_FALSE(b.history.longPending());               // loaded records are already saved
}

// ---- charging sessions ----

void test_a_session_reports_the_wallbox_energy_and_our_sun_share() {
    Rig r;
    r.minute(3000, 0, 0);                              // starts the clock
    r.history.sessionStarted(5000000);
    for (int i = 0; i < 60; i++) r.minute(3000, -500, 2400);     // an hour at 2.4 kW; home = 3000 - 500 - 2400 = 100
    r.history.sessionStopped(5002350, "Remote");        // the wallbox's own meter says 2350 Wh
    auto done = r.history.recentSessions(5);
    TEST_ASSERT_EQUAL(1, done.size());
    TEST_ASSERT_FLOAT_WITHIN(0.5, 2350, done[0].energyWh());
    TEST_ASSERT_FLOAT_WITHIN(5, 2400, done[0].estimatedWh);           // our estimate is close
    TEST_ASSERT_FLOAT_WITHIN(5, 2400, done[0].sunWh);                  // the sun covered all of it
    testing::check("Remote", done[0].reason);
    TEST_ASSERT_NULL(r.history.openSession());
    TEST_ASSERT_TRUE(testing::contains(r.history.recentEvents(1)[0].text, "kWh, 100% from the sun"));
}

void test_the_estimate_is_used_when_the_wallbox_gives_no_meter_readings() {
    Rig r;
    r.minute(2000, 0, 0);
    r.history.sessionStarted(-1);
    for (int i = 0; i < 30; i++) r.minute(500, 1900, 2400);    // grid-powered: home = 500 + 1900 - 2400 = 0, 500 W of sun
    r.history.sessionStopped(-1, "");
    auto done = r.history.recentSessions(1)[0];
    TEST_ASSERT_FLOAT_WITHIN(5, 1200, done.energyWh());               // 2.4 kW for half an hour
    TEST_ASSERT_FLOAT_WITHIN(5, 250, done.sunWh);                     // 500 W of sun for half an hour
}

void test_a_session_in_progress_can_be_seen_and_survives_a_restart() {
    Rig a;
    a.minute(2000, 0, 0);
    a.history.sessionStarted(100);
    for (int i = 0; i < 10; i++) a.minute(3000, 0, 1000);
    TEST_ASSERT_NOT_NULL(a.history.openSession());
    TEST_ASSERT_TRUE(a.history.openSession()->estimatedWh > 100);

    std::vector<std::string> lines;
    a.history.forEachLine([&](const std::string& l) { lines.push_back(l); });
    Rig b;
    b.platform.epoch = a.platform.epoch;
    for (const auto& l : lines) b.history.loadLine(l);
    TEST_ASSERT_NOT_NULL(b.history.openSession());
    TEST_ASSERT_FLOAT_WITHIN(0.1, a.history.openSession()->estimatedWh, b.history.openSession()->estimatedWh);
    b.minute(3000, 0, 1000);
    b.history.sessionStopped(100 + 500, "EVDisconnected");             // the stop arrives after the restart
    TEST_ASSERT_EQUAL(1, b.history.recentSessions(5).size());
    TEST_ASSERT_FLOAT_WITHIN(0.5, 500, b.history.recentSessions(5)[0].energyWh());
}

void test_finished_sessions_survive_being_saved_and_loaded() {
    Rig a;
    a.minute(2000, 0, 0);
    for (int s = 0; s < 3; s++) {
        a.history.sessionStarted(s * 1000);
        for (int i = 0; i < 20; i++) a.minute(3000, 0, 1000);
        a.history.sessionStopped(s * 1000 + 300, "Remote");
    }
    std::vector<std::string> lines;
    a.history.forEachLine([&](const std::string& l) { lines.push_back(l); });
    Rig b;
    for (const auto& l : lines) b.history.loadLine(l);
    TEST_ASSERT_EQUAL(3, b.history.recentSessions(10).size());
    TEST_ASSERT_EQUAL(a.history.recentSessions(1)[0].start, b.history.recentSessions(1)[0].start);   // newest first
}

void test_a_stop_without_a_start_is_ignored_and_a_new_start_closes_a_lost_one() {
    Rig r;
    r.minute(1000, 0, 0);
    r.history.sessionStopped(500, "Remote");
    TEST_ASSERT_EQUAL(0, r.history.recentSessions(5).size());

    r.history.sessionStarted(0);
    r.minute(1000, 0, 0);
    r.history.sessionStarted(10);                      // the first one never reported its end
    TEST_ASSERT_EQUAL(1, r.history.recentSessions(5).size());
    testing::check("unknown", r.history.recentSessions(5)[0].reason);
    TEST_ASSERT_NOT_NULL(r.history.openSession());
}

void test_only_the_latest_four_hundred_sessions_are_kept() {
    Rig r;
    r.minute(1000, 0, 0);
    for (size_t i = 0; i < HistoryService::kMaxSessions + 20; i++) {
        r.history.sessionStarted(0);
        r.minute(1000, 0, 500);
        r.history.sessionStopped(0, "Remote");
    }
    TEST_ASSERT_EQUAL(HistoryService::kMaxSessions, r.history.recentSessions(1000).size());
}

// ---- retention by age: activity one day, finished sessions 30 days; the graph's data is never touched ---------------------------

void test_activity_lines_older_than_a_day_leave_the_list_at_once_and_are_purged_later() {
    Rig r;
    r.minute(1000, 0, 0);
    const int64_t t0 = r.platform.epoch;
    r.history.addEvent("old");
    r.platform.epoch = t0 + 3 * 3600;
    r.history.addEvent("middle");
    r.platform.epoch = t0 + 23 * 3600;
    r.history.addEvent("recent");
    TEST_ASSERT_EQUAL(3, r.history.eventCount());
    r.platform.epoch = t0 + 25 * 3600;                       // "old" is now 25 hours old; nothing has been recorded since
    TEST_ASSERT_EQUAL(2, r.history.eventCount());
    auto shown = r.history.recentEvents(10);
    TEST_ASSERT_EQUAL(2, shown.size());
    testing::check("recent", shown[0].text);
    testing::check("middle", shown[1].text);
    TEST_ASSERT_EQUAL(1, r.history.recentEvents(10, 1).size());   // paging counts only what is shown
    r.minute(1000, 0, 0);                                    // the once-a-minute purge removes it for good
    int lines = 0;
    r.history.forEachLine([&](const std::string& l) { if (l.rfind("E\t", 0) == 0) lines++; });
    TEST_ASSERT_EQUAL(2, lines);
    r.platform.epoch = t0 + 60 * 3600;
    r.minute(1000, 0, 0);
    TEST_ASSERT_EQUAL(0, r.history.eventCount());
}

void test_finished_sessions_older_than_thirty_days_are_dropped() {
    Rig r;
    r.minute(1000, 0, 0);
    r.history.sessionStarted(0);
    r.minute(1000, 0, 500);
    r.history.sessionStopped(0, "Remote");                   // an old charge
    const int64_t t0 = r.platform.epoch;
    r.platform.epoch = t0 + 20 * 86400;
    r.minute(1000, 0, 0);
    r.history.sessionStarted(0);
    r.minute(1000, 0, 500);
    r.history.sessionStopped(0, "Remote");                   // a newer one
    TEST_ASSERT_EQUAL(2, r.history.sessionCount());
    r.platform.epoch = t0 + 31 * 86400;                      // the first one is now older than 30 days
    TEST_ASSERT_EQUAL(1, r.history.sessionCount());
    TEST_ASSERT_EQUAL(1, r.history.recentSessions(10).size());
    r.minute(1000, 0, 0);
    int lines = 0;
    r.history.forEachLine([&](const std::string& l) { if (l.rfind("T\t", 0) == 0) lines++; });
    TEST_ASSERT_EQUAL(1, lines);
    r.platform.epoch = t0 + 60 * 86400;
    r.minute(1000, 0, 0);
    TEST_ASSERT_EQUAL(0, r.history.sessionCount());
}

void test_without_a_valid_clock_nothing_is_purged() {
    Rig r;
    r.minute(1000, 0, 0);
    r.history.addEvent("kept");
    r.platform.epoch = 1000;                                 // clock lost: everything looks 'old' but must not vanish
    TEST_ASSERT_EQUAL(1, r.history.eventCount());
    TEST_ASSERT_EQUAL(1, r.history.recentEvents(10).size());
}

void test_purging_never_touches_the_graph_data() {
    // Two identical runs of 45 days; one also logs events and sessions all along. Their graph data must be identical.
    Rig a, b;
    for (int step = 0; step < 45 * 288; step++) {
        const double sun = (step % 288) > 80 && (step % 288) < 220 ? 4000 : 0;
        a.platform.epoch += 300; b.platform.epoch += 300;
        a.history.recordSample(sun, -500, sun > 0 ? 2000 : 0, ChargingMode::Eco, false);
        b.history.recordSample(sun, -500, sun > 0 ? 2000 : 0, ChargingMode::Eco, false);
        if (step % 20 == 0) a.history.addEvent("event " + std::to_string(step));
        if (step % 288 == 100) { a.history.sessionStarted(0); }
        if (step % 288 == 200) { a.history.sessionStopped(0, "Remote"); }
    }
    TEST_ASSERT_TRUE(a.history.eventCount() < 20);           // only about the last day of activity is left
    TEST_ASSERT_TRUE(a.history.sessionCount() <= 31);        // only about the last 30 days of charges
    TEST_ASSERT_TRUE(b.history.sessionCount() == 0 && b.history.eventCount() == 0);
    TEST_ASSERT_EQUAL(b.history.allDays().size(), a.history.allDays().size());
    for (const auto& kv : b.history.allDays()) {
        const auto& x = kv.second;
        const auto& y = a.history.allDays().at(kv.first);
        TEST_ASSERT_FLOAT_WITHIN(0.01, x.solarWh, y.solarWh);
        TEST_ASSERT_FLOAT_WITHIN(0.01, x.carWh, y.carWh);
        TEST_ASSERT_FLOAT_WITHIN(0.01, x.importWh, y.importWh);
        TEST_ASSERT_FLOAT_WITHIN(0.01, x.exportWh, y.exportWh);
    }
    for (int stepSeconds : {60, 300, 3600}) {
        auto sa = a.history.series(0, stepSeconds), sb = b.history.series(0, stepSeconds);
        TEST_ASSERT_EQUAL(sb.size(), sa.size());
        for (size_t i = 0; i < sa.size(); i++) {
            TEST_ASSERT_EQUAL(sb[i].t, sa[i].t);
            TEST_ASSERT_FLOAT_WITHIN(0.01, sb[i].solar, sa[i].solar);
            TEST_ASSERT_FLOAT_WITHIN(0.01, sb[i].car, sa[i].car);
        }
    }
    TEST_ASSERT_EQUAL(b.history.minuteSampleCount(), a.history.minuteSampleCount());
}

// ---- a run of limit changes is one line ----------------------------------------------------------------------------------------

void test_repeated_limit_changes_become_one_line_that_counts_them_and_shows_the_latest() {
    Rig r;
    r.minute(1000, 0, 0);
    std::vector<std::string> heard;
    r.history.setEventListener([&](const std::string& t) { heard.push_back(t); });
    r.history.addEvent("Changed limit to 9 A: less solar left");
    TEST_ASSERT_EQUAL(1, r.history.eventCount());
    testing::check("Changed limit to 9 A: less solar left", r.history.recentEvents(1)[0].text);   // one change stays as it is
    r.platform.epoch += 600;
    r.history.addEvent("Changed limit to 10 A: more solar");
    r.platform.epoch += 600;
    r.history.addEvent("Changed limit to 8 A: less solar left");
    r.platform.epoch += 600;
    r.history.addEvent("Changed limit to 13 A: more solar");
    TEST_ASSERT_EQUAL(1, r.history.eventCount());
    auto e = r.history.recentEvents(5)[0];
    testing::check("Changed limit 4 times, now 13 A: more solar", e.text);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(r.platform.epoch), e.t);   // the time of the latest change
    TEST_ASSERT_EQUAL(4, heard.size());                                      // the messages feature still hears every real change
    testing::check("Changed limit to 13 A: more solar", heard.back());
}

void test_anything_else_in_between_or_a_gap_of_an_hour_starts_a_new_line() {
    Rig r;
    r.minute(1000, 0, 0);
    r.history.addEvent("Changed limit to 9 A: more solar");
    r.platform.epoch += 60;
    r.history.addEvent("Changed limit to 10 A: more solar");
    r.platform.epoch += 60;
    r.history.addEvent("Car plugged in");                                    // another kind of line breaks the run
    r.platform.epoch += 60;
    r.history.addEvent("Changed limit to 11 A: more solar");
    r.platform.epoch += 3600;                                                // an hour later: a new run
    r.history.addEvent("Changed limit to 12 A: more solar");
    auto events = r.history.recentEvents(10);
    TEST_ASSERT_EQUAL(4, events.size());
    testing::check("Changed limit to 12 A: more solar", events[0].text);
    testing::check("Changed limit to 11 A: more solar", events[1].text);
    testing::check("Car plugged in", events[2].text);
    testing::check("Changed limit 2 times, now 10 A: more solar", events[3].text);
}

void test_the_wallbox_and_the_car_limit_lines_never_merge_and_a_missing_reason_is_fine() {
    Rig r;
    r.minute(1000, 0, 0);
    r.history.addEvent("Changed limit to 9 A");
    r.platform.epoch += 60;
    r.history.addEvent("Car: changed limit to 10 A: more solar");             // the car's own commands
    r.platform.epoch += 60;
    r.history.addEvent("Car: changed limit to 11 A: more solar");
    r.platform.epoch += 60;
    r.history.addEvent("Car: changed limit to 12 A");
    auto events = r.history.recentEvents(10);
    TEST_ASSERT_EQUAL(2, events.size());
    testing::check("Car: changed limit 3 times, now 12 A", events[0].text);
    testing::check("Changed limit to 9 A", events[1].text);
    r.platform.epoch += 60;
    r.history.addEvent("Changed limit to abc A: odd");                        // an unreadable line is just a line
    TEST_ASSERT_EQUAL(3, r.history.eventCount());
}

void test_a_merged_limit_line_survives_saving_and_loading() {
    Rig a;
    a.minute(1000, 0, 0);
    a.history.addEvent("Changed limit to 9 A: more solar");
    a.platform.epoch += 60;
    a.history.addEvent("Changed limit to 10 A: more solar");
    std::vector<std::string> lines;
    a.history.forEachLine([&](const std::string& l) { lines.push_back(l); });
    Rig b;
    b.platform.epoch = a.platform.epoch;
    for (const auto& l : lines) b.history.loadLine(l);
    testing::check("Changed limit 2 times, now 10 A: more solar", b.history.recentEvents(1)[0].text);
    b.platform.epoch += 60;
    b.history.addEvent("Changed limit to 8 A: less solar left");               // and the run carries on after a restart
    testing::check("Changed limit 3 times, now 8 A: less solar left", b.history.recentEvents(1)[0].text);
}

void test_a_year_of_days_is_kept() {
    TEST_ASSERT_TRUE(HistoryService::kMaxDays >= 366);
}

void test_a_session_remembers_when_the_car_was_plugged_in() {
    Rig a;
    a.minute(2000, 0, 0);
    a.history.notePluggedIn();
    const uint32_t pluggedAt = static_cast<uint32_t>(a.platform.epoch);
    for (int i = 0; i < 90; i++) a.minute(2000, 0, 0);
    a.history.sessionStarted(0);
    TEST_ASSERT_TRUE(a.history.openSession()->pluggedAt == pluggedAt);
    for (int i = 0; i < 10; i++) a.minute(3000, 0, 1000);
    a.history.sessionStopped(100, "Remote");
    TEST_ASSERT_TRUE(a.history.recentSessions(1)[0].pluggedAt == pluggedAt);
    std::vector<std::string> lines;
    a.history.forEachLine([&](const std::string& l) { lines.push_back(l); });
    Rig b;
    for (const auto& l : lines) b.history.loadLine(l);
    TEST_ASSERT_TRUE(b.history.recentSessions(1)[0].pluggedAt == pluggedAt);
}

void test_no_plug_in_seen_or_an_old_one_gives_no_plug_in_time() {
    Rig r;
    r.minute(2000, 0, 0);
    r.history.sessionStarted(0);
    TEST_ASSERT_EQUAL(0, (int)r.history.openSession()->pluggedAt);
    r.history.sessionStopped(0, "Remote");
    r.history.notePluggedIn();
    r.history.notePluggedOut();
    r.history.sessionStarted(0);
    TEST_ASSERT_EQUAL(0, (int)r.history.openSession()->pluggedAt);
    r.history.sessionStopped(0, "Remote");
    r.history.notePluggedIn();
    r.platform.epoch += 4 * 86400;                 // four days later
    r.history.sessionStarted(0);
    TEST_ASSERT_EQUAL(0, (int)r.history.openSession()->pluggedAt);
}

void test_a_goal_result_is_kept_per_day_survives_saving_and_old_files_still_load() {
    Rig a;
    a.minute(2000, 0, 0);
    a.history.recordGoalResult(a.platform.epoch, true, 20000, 21500);
    TEST_ASSERT_EQUAL(1, a.history.goalOutcome(a.today()));
    TEST_ASSERT_EQUAL(0, a.history.goalOutcome(a.today() - 1));
    a.history.recordGoalResult(a.platform.epoch - 86400, false, 20000, 9000);
    TEST_ASSERT_EQUAL(2, a.history.goalOutcome(a.today() - 1));
    bool metText = false, missedText = false;
    for (const auto& e : a.history.recentEvents(10)) {
        if (e.text.rfind("Departure goal met: 21.5 of 20.0 kWh", 0) == 0) metText = true;
        if (e.text.rfind("Departure goal missed", 0) == 0) missedText = true;
    }
    TEST_ASSERT_TRUE(metText);
    TEST_ASSERT_TRUE(missedText);
    std::vector<std::string> lines;
    a.history.forEachLine([&](const std::string& l) { lines.push_back(l); });
    bool hasG = false;
    for (const auto& l : lines) if (l.rfind("G\t", 0) == 0) hasG = true;
    TEST_ASSERT_TRUE(hasG);
    Rig b;
    for (const auto& l : lines) b.history.loadLine(l);
    TEST_ASSERT_EQUAL(1, b.history.goalOutcome(b.today()));
    TEST_ASSERT_EQUAL(2, b.history.goalOutcome(b.today() - 1));
    Rig old;                                        // a file from before goal results existed
    for (const auto& l : lines) if (l.rfind("G\t", 0) != 0) old.history.loadLine(l);
    TEST_ASSERT_EQUAL(0, old.history.goalOutcome(old.today()));
}

void test_the_journal_carries_recent_goal_results() {
    Rig r;
    r.minute(2000, 0, 0);
    r.history.markClean();
    r.minute(2000, 0, 0);
    r.history.recordGoalResult(r.platform.epoch, true, 20000, 22000);
    bool hasG = false;
    r.history.forEachJournalLine([&](const std::string& l) { if (l.rfind("G\t", 0) == 0) hasG = true; });
    TEST_ASSERT_TRUE(hasG);
}

void test_the_ring_drops_the_oldest_and_can_use_a_custom_allocator() {
    Ring<int> ring(3);
    for (int i = 1; i <= 5; i++) ring.push(i);
    TEST_ASSERT_EQUAL(3, ring.size());
    TEST_ASSERT_EQUAL(3, ring.at(0));
    TEST_ASSERT_EQUAL(5, ring.at(2));

    static int calls = 0;
    bigAllocator() = [](size_t bytes) -> void* { calls++; return std::malloc(bytes); };
    Ring<int> other(4);
    other.push(7);
    bigAllocator() = nullptr;
    TEST_ASSERT_EQUAL(1, calls);
    TEST_ASSERT_EQUAL(7, other.at(0));

    bigAllocator() = [](size_t) -> void* { return nullptr; };   // out of memory: nothing stored, no crash
    Ring<int> none(4);
    none.push(1);
    bigAllocator() = nullptr;
    TEST_ASSERT_EQUAL(0, none.size());
}

}  // namespace


void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_home_is_what_is_left_after_the_car);
    RUN_TEST(test_energy_is_counted_between_readings_and_split_into_solar_and_grid);
    RUN_TEST(test_the_car_part_the_sun_cannot_cover_counts_as_grid);
    RUN_TEST(test_a_long_gap_without_readings_is_not_counted_as_energy);
    RUN_TEST(test_energy_goes_into_the_local_day_not_the_utc_day);
    RUN_TEST(test_the_start_of_the_local_day_is_local_midnight);
    RUN_TEST(test_samples_can_be_averaged_into_buckets);
    RUN_TEST(test_a_bucket_remembers_that_charging_happened_in_it);
    RUN_TEST(test_only_the_last_48_hours_are_kept);
    RUN_TEST(test_events_come_back_newest_first_and_repeats_are_dropped);
    RUN_TEST(test_the_rate_schedule_gives_the_peak_price_only_in_the_peak_hours);
    RUN_TEST(test_a_session_remembers_its_mode_and_whether_it_changed);
    RUN_TEST(test_money_saved_uses_the_price_of_the_hour_and_survives_saving);
    RUN_TEST(test_going_back_to_an_earlier_setting_is_still_logged);
    RUN_TEST(test_the_event_log_keeps_the_latest_fifty);
    RUN_TEST(test_everything_survives_being_saved_and_loaded);
    RUN_TEST(test_loading_garbage_does_no_harm);
    RUN_TEST(test_nothing_is_recorded_before_the_clock_is_set);
    RUN_TEST(test_seconds_since_boot_do_not_count_as_a_set_clock);
    RUN_TEST(test_dirty_tracks_unsaved_changes);
    RUN_TEST(test_minutes_are_rolled_up_into_five_minute_records);
    RUN_TEST(test_hours_are_rolled_up_from_the_five_minute_records);
    RUN_TEST(test_an_old_day_comes_from_the_coarser_tiers);
    RUN_TEST(test_recent_time_still_uses_the_minute_samples);
    RUN_TEST(test_only_new_long_records_are_written_when_asked);
    RUN_TEST(test_the_long_records_survive_being_saved_and_loaded);
    RUN_TEST(test_a_session_reports_the_wallbox_energy_and_our_sun_share);
    RUN_TEST(test_the_estimate_is_used_when_the_wallbox_gives_no_meter_readings);
    RUN_TEST(test_a_session_in_progress_can_be_seen_and_survives_a_restart);
    RUN_TEST(test_finished_sessions_survive_being_saved_and_loaded);
    RUN_TEST(test_a_stop_without_a_start_is_ignored_and_a_new_start_closes_a_lost_one);
    RUN_TEST(test_only_the_latest_four_hundred_sessions_are_kept);
    RUN_TEST(test_activity_lines_older_than_a_day_leave_the_list_at_once_and_are_purged_later);
    RUN_TEST(test_finished_sessions_older_than_thirty_days_are_dropped);
    RUN_TEST(test_without_a_valid_clock_nothing_is_purged);
    RUN_TEST(test_purging_never_touches_the_graph_data);
    RUN_TEST(test_repeated_limit_changes_become_one_line_that_counts_them_and_shows_the_latest);
    RUN_TEST(test_anything_else_in_between_or_a_gap_of_an_hour_starts_a_new_line);
    RUN_TEST(test_the_wallbox_and_the_car_limit_lines_never_merge_and_a_missing_reason_is_fine);
    RUN_TEST(test_a_merged_limit_line_survives_saving_and_loading);
    RUN_TEST(test_a_year_of_days_is_kept);
    RUN_TEST(test_the_journal_on_top_of_the_full_save_restores_everything);
    RUN_TEST(test_a_session_that_ended_after_the_full_save_is_not_left_open);
    RUN_TEST(test_a_session_remembers_when_the_car_was_plugged_in);
    RUN_TEST(test_no_plug_in_seen_or_an_old_one_gives_no_plug_in_time);
    RUN_TEST(test_a_goal_result_is_kept_per_day_survives_saving_and_old_files_still_load);
    RUN_TEST(test_the_journal_carries_recent_goal_results);
    RUN_TEST(test_the_ring_drops_the_oldest_and_can_use_a_custom_allocator);
    return UNITY_END();
}
