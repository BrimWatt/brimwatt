// Host-side tests for Cheapest hours: which hours are expensive, weekends, windows past midnight, time to the next change.
#include "../support/TestSupport.h"

#include "planner/CheapHours.h"

using solar::CheapHours;

namespace {

constexpr int kPdt = -7 * 3600;
// Wednesday 5 Nov 2025, 00:00 local (PST would be -8, but the fixed offset keeps the arithmetic plain).
constexpr int64_t kWedMidnight = 1762326000;   // 2025-11-05 07:00 UTC

int64_t at(int dayOffset, int hour, int minute = 0) { return kWedMidnight + dayOffset * 86400 + hour * 3600 + minute * 60; }

void test_the_window_is_expensive_and_the_rest_is_cheap() {
    TEST_ASSERT_FALSE(CheapHours::expensive(at(0, 15, 59), kPdt, 16, 21, true));
    TEST_ASSERT_TRUE(CheapHours::expensive(at(0, 16), kPdt, 16, 21, true));
    TEST_ASSERT_TRUE(CheapHours::expensive(at(0, 20, 59), kPdt, 16, 21, true));
    TEST_ASSERT_FALSE(CheapHours::expensive(at(0, 21), kPdt, 16, 21, true));
}

void test_a_window_past_midnight_works() {
    TEST_ASSERT_TRUE(CheapHours::expensive(at(0, 23), kPdt, 22, 6, true));
    TEST_ASSERT_TRUE(CheapHours::expensive(at(0, 5, 59), kPdt, 22, 6, true));
    TEST_ASSERT_FALSE(CheapHours::expensive(at(0, 6), kPdt, 22, 6, true));
}

void test_weekends_are_cheap_when_the_window_is_weekdays_only() {
    TEST_ASSERT_TRUE(CheapHours::expensive(at(0, 17), kPdt, 16, 21, false));    // Wednesday
    TEST_ASSERT_FALSE(CheapHours::expensive(at(3, 17), kPdt, 16, 21, false));   // Saturday
    TEST_ASSERT_FALSE(CheapHours::expensive(at(4, 17), kPdt, 16, 21, false));   // Sunday
    TEST_ASSERT_TRUE(CheapHours::expensive(at(3, 17), kPdt, 16, 21, true));
    TEST_ASSERT_TRUE(CheapHours::expensive(at(5, 17), kPdt, 16, 21, false));    // Monday again
}

void test_seconds_to_the_next_change() {
    auto offset = [](int64_t) { return kPdt; };
    TEST_ASSERT_EQUAL_INT64(30 * 60, CheapHours::secondsToChange(at(0, 15, 30), offset, 16, 21, true));
    TEST_ASSERT_EQUAL_INT64(3 * 3600, CheapHours::secondsToChange(at(0, 18), offset, 16, 21, true));
}

void test_on_friday_night_with_weekdays_only_the_next_change_is_monday_afternoon() {
    auto offset = [](int64_t) { return kPdt; };
    const int64_t friNight = at(2, 22);                  // Friday 10 pm
    TEST_ASSERT_EQUAL_INT64(at(5, 16) - friNight, CheapHours::secondsToChange(friNight, offset, 16, 21, false));
}

void test_a_window_that_never_changes_reports_minus_one() {
    auto offset = [](int64_t) { return kPdt; };
    TEST_ASSERT_EQUAL_INT64(-1, CheapHours::secondsToChange(at(0, 12), offset, 0, 24, true));   // expensive all day, every day
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_window_is_expensive_and_the_rest_is_cheap);
    RUN_TEST(test_a_window_past_midnight_works);
    RUN_TEST(test_weekends_are_cheap_when_the_window_is_weekdays_only);
    RUN_TEST(test_seconds_to_the_next_change);
    RUN_TEST(test_on_friday_night_with_weekdays_only_the_next_change_is_monday_afternoon);
    RUN_TEST(test_a_window_that_never_changes_reports_minus_one);
    return UNITY_END();
}
