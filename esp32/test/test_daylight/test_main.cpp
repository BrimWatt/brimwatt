// Host-side tests for the sun calculation and the daylight window.
#include "../support/TestSupport.h"

#include <cstdlib>

#include "common/CivilDate.h"
#include "daylight/DaylightWindow.h"
#include "daylight/SunCalculator.h"

using namespace solar;

namespace {

constexpr int kPacific = -7 * 3600;   // PDT

int64_t day(int y, int m, int d) { return daysFromCivil(y, m, d); }

int minutesOfDay(int64_t epoch, int offset) {
    int64_t local = epoch + offset;
    return static_cast<int>((local - floorDiv(local, 86400) * 86400) / 60);
}

void assertTime(int expectedHour, int expectedMinute, int64_t epoch, int offset, const char* what) {
    int diff = std::abs(minutesOfDay(epoch, offset) - (expectedHour * 60 + expectedMinute));
    char message[80];
    snprintf(message, sizeof message, "%s: expected about %02d:%02d, was %02d:%02d", what, expectedHour, expectedMinute,
             minutesOfDay(epoch, offset) / 60, minutesOfDay(epoch, offset) % 60);
    TEST_ASSERT_TRUE_MESSAGE(diff <= 3, message);
}

SunTimes sun(int y, int m, int d, double lat, double lon, int offset) {
    return calculateSun(day(y, m, d), lat, lon, [offset](int64_t) { return offset; });
}

// ---- sun times against published values ----

void test_london_summer_solstice() {
    SunTimes s = sun(2024, 6, 21, 51.4779, -0.0015, 3600);
    TEST_ASSERT_EQUAL(static_cast<int>(SunTimes::Kind::Normal), static_cast<int>(s.kind));
    assertTime(4, 43, s.sunrise, 3600, "sunrise");
    assertTime(21, 21, s.sunset, 3600, "sunset");
}

void test_new_york_winter_solstice() {
    SunTimes s = sun(2024, 12, 21, 40.7128, -74.0060, -5 * 3600);
    assertTime(7, 17, s.sunrise, -5 * 3600, "sunrise");
    assertTime(16, 32, s.sunset, -5 * 3600, "sunset");
}

void test_lathrop_summer_solstice() {
    SunTimes s = sun(2025, 6, 21, 37.8412, -121.2691, kPacific);
    assertTime(5, 43, s.sunrise, kPacific, "sunrise");
    assertTime(20, 30, s.sunset, kPacific, "sunset");
}

void test_lathrop_winter_days_are_about_five_hours_shorter() {
    SunTimes winter = sun(2025, 12, 21, 37.8412, -121.2691, -8 * 3600);
    SunTimes summer = sun(2025, 6, 21, 37.8412, -121.2691, kPacific);
    int winterMinutes = static_cast<int>((winter.sunset - winter.sunrise) / 60);
    int summerMinutes = static_cast<int>((summer.sunset - summer.sunrise) / 60);
    TEST_ASSERT_TRUE(winterMinutes > 9 * 60 && winterMinutes < 10 * 60);
    TEST_ASSERT_TRUE(summerMinutes > 14 * 60 + 30 && summerMinutes < 15 * 60 + 30);
}

void test_sydney_east_of_greenwich_keeps_the_local_date() {
    int offset = 11 * 3600;
    SunTimes s = sun(2024, 12, 21, -33.8688, 151.2093, offset);
    TEST_ASSERT_EQUAL(day(2024, 12, 21), floorDiv(s.sunrise + offset, 86400));
    TEST_ASSERT_EQUAL(day(2024, 12, 21), floorDiv(s.sunset + offset, 86400));
    assertTime(5, 41, s.sunrise, offset, "sunrise");
    assertTime(20, 5, s.sunset, offset, "sunset");
}

void test_the_equator_at_the_equinox_has_about_twelve_hours_of_day() {
    SunTimes s = sun(2024, 3, 20, 0.0, 0.0, 0);
    int minutes = static_cast<int>((s.sunset - s.sunrise) / 60);
    TEST_ASSERT_TRUE(minutes >= 12 * 60 + 3 && minutes <= 12 * 60 + 12);
}

void test_polar_day_and_night() {
    SunTimes june = sun(2024, 6, 21, 69.6492, 18.9553, 2 * 3600);     // Tromso
    SunTimes december = sun(2024, 12, 21, 69.6492, 18.9553, 3600);
    TEST_ASSERT_EQUAL(static_cast<int>(SunTimes::Kind::AlwaysUp), static_cast<int>(june.kind));
    TEST_ASSERT_EQUAL(static_cast<int>(SunTimes::Kind::AlwaysDown), static_cast<int>(december.kind));
}

// ---- the window ----

struct DefaultSite {
    testing::FakePlatform platform;
    DaylightWindow window;
    DefaultSite(int afterSunrise = 3600, int beforeSunset = 3600)
        : window(platform, 37.8412, -121.2691, afterSunrise, beforeSunset) {}

    int64_t at(int y, int m, int d, int hour, int minute) const {
        return day(y, m, d) * 86400 + hour * 3600 + minute * 60 - platform.offsetSeconds;
    }
};

void test_the_window_is_sunrise_to_sunset_shrunk_by_the_margins() {
    DefaultSite l;
    SunTimes s = sun(2025, 6, 21, 37.8412, -121.2691, kPacific);
    auto span = l.window.spanFor(day(2025, 6, 21));
    TEST_ASSERT_TRUE(span.has_value());
    TEST_ASSERT_EQUAL(s.sunrise + 3600, span->start);
    TEST_ASSERT_EQUAL(s.sunset - 3600, span->end);
}

void test_the_margins_can_be_changed_later() {
    DefaultSite l;
    SunTimes s = sun(2025, 6, 21, 37.8412, -121.2691, kPacific);
    l.window.setMargins(1800, 3600);
    auto span = l.window.spanFor(day(2025, 6, 21));
    TEST_ASSERT_EQUAL(s.sunrise + 1800, span->start);
    TEST_ASSERT_EQUAL(s.sunset - 3600, span->end);
}

void test_it_opens_and_closes_exactly_at_the_edges() {
    DefaultSite l;
    auto span = l.window.spanFor(day(2025, 6, 21));
    TEST_ASSERT_FALSE(l.window.isOpen(span->start - 1));
    TEST_ASSERT_TRUE(l.window.isOpen(span->start));
    TEST_ASSERT_TRUE(l.window.isOpen(span->end - 1));
    TEST_ASSERT_FALSE(l.window.isOpen(span->end));
}

void test_closed_at_night_and_open_at_midday() {
    DefaultSite l;
    TEST_ASSERT_FALSE(l.window.isOpen(l.at(2025, 6, 21, 2, 0)));
    TEST_ASSERT_TRUE(l.window.isOpen(l.at(2025, 6, 21, 13, 0)));
    TEST_ASSERT_FALSE(l.window.isOpen(l.at(2025, 6, 21, 20, 45)));
}

void test_the_winter_window_is_shorter_than_the_summer_window() {
    DefaultSite l;
    auto summer = l.window.spanFor(day(2025, 6, 21));
    auto winter = l.window.spanFor(day(2025, 12, 21));
    TEST_ASSERT_TRUE(winter->end - winter->start < summer->end - summer->start);
}

void test_no_window_when_the_margins_swallow_the_day() {
    DefaultSite l(8 * 3600, 8 * 3600);
    TEST_ASSERT_FALSE(l.window.spanFor(day(2025, 12, 21)).has_value());
    TEST_ASSERT_FALSE(l.window.isOpen(l.at(2025, 12, 21, 12, 0)));
}

void test_the_description_shows_local_clock_times() {
    DefaultSite l;
    std::string text = l.window.describe(l.at(2025, 6, 21, 12, 0));
    TEST_ASSERT_EQUAL(14, text.size());
    TEST_ASSERT_EQUAL(':', text[2]);
    TEST_ASSERT_TRUE(testing::contains(text, " to "));
    TEST_ASSERT_EQUAL('0', text[0]);   // morning start is 06:xx in June
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_london_summer_solstice);
    RUN_TEST(test_new_york_winter_solstice);
    RUN_TEST(test_lathrop_summer_solstice);
    RUN_TEST(test_lathrop_winter_days_are_about_five_hours_shorter);
    RUN_TEST(test_sydney_east_of_greenwich_keeps_the_local_date);
    RUN_TEST(test_the_equator_at_the_equinox_has_about_twelve_hours_of_day);
    RUN_TEST(test_polar_day_and_night);
    RUN_TEST(test_the_window_is_sunrise_to_sunset_shrunk_by_the_margins);
    RUN_TEST(test_the_margins_can_be_changed_later);
    RUN_TEST(test_it_opens_and_closes_exactly_at_the_edges);
    RUN_TEST(test_closed_at_night_and_open_at_midday);
    RUN_TEST(test_the_winter_window_is_shorter_than_the_summer_window);
    RUN_TEST(test_no_window_when_the_margins_swallow_the_day);
    RUN_TEST(test_the_description_shows_local_clock_times);
    return UNITY_END();
}
