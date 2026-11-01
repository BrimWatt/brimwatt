// Host-side tests for the memory watch: the 5-minute samples, how many it keeps, and when the lowest point is noticed.
#include "../support/TestSupport.h"

#include "common/MemoryWatch.h"

using solar::MemoryWatch;

namespace {

void test_a_sample_is_kept_at_the_first_look_and_then_every_five_minutes() {
    MemoryWatch w;
    w.update(10, 0, 150, 130, 120, 6000);
    TEST_ASSERT_EQUAL(1, (int)w.size());
    w.update(15, 0, 149, 130, 119, 6000);     // 5 s later: too soon
    w.update(309, 0, 148, 130, 118, 6000);    // 299 s after the first: still too soon
    TEST_ASSERT_EQUAL(1, (int)w.size());
    w.update(310, 0, 147, 130, 117, 5990);    // 300 s
    TEST_ASSERT_EQUAL(2, (int)w.size());
    TEST_ASSERT_EQUAL(150, w.sample(0).freeKb);
    TEST_ASSERT_EQUAL(147, w.sample(1).freeKb);
    TEST_ASSERT_EQUAL(117, w.sample(1).largestKb);
    TEST_ASSERT_EQUAL(5990, w.sample(1).psramKb);
    TEST_ASSERT_EQUAL(310, (int)w.newestAtUptimeSeconds());
}

void test_it_keeps_a_day_and_drops_the_oldest() {
    MemoryWatch w;
    for (uint32_t i = 0; i < MemoryWatch::kCapacity + 10; i++) w.update(i * 300, 0, 100 + (int)(i % 50), 90, 80, 1000);
    TEST_ASSERT_EQUAL((int)MemoryWatch::kCapacity, (int)w.size());
    TEST_ASSERT_EQUAL(100 + 10, w.sample(0).freeKb);   // sample number 10 is now the oldest
}

void test_the_lowest_point_is_the_start_up_figure_until_it_drops_later() {
    MemoryWatch w;
    TEST_ASSERT_FALSE(w.lowest().known);
    w.update(20, 0, 150, 88, 95, 6000);   // the platform already reports a low from boot
    TEST_ASSERT_TRUE(w.lowest().known);
    TEST_ASSERT_TRUE(w.lowest().beforeFirstCheck);
    TEST_ASSERT_EQUAL(88, w.lowest().minFreeKb);
    w.update(25, 0, 150, 88, 95, 6000);   // unchanged: stays the start-up figure
    TEST_ASSERT_TRUE(w.lowest().beforeFirstCheck);
    w.update(900, 1760000000, 70, 61, 40, 6000);   // a new low, noticed now
    TEST_ASSERT_FALSE(w.lowest().beforeFirstCheck);
    TEST_ASSERT_EQUAL(61, w.lowest().minFreeKb);
    TEST_ASSERT_EQUAL(900, (int)w.lowest().atUptimeSeconds);
    TEST_ASSERT_TRUE(w.lowest().atEpoch == 1760000000);
    TEST_ASSERT_EQUAL(70, w.lowest().freeKb);
    TEST_ASSERT_EQUAL(40, w.lowest().largestKb);
    w.update(1200, 0, 140, 61, 90, 6000);   // not lower: nothing changes
    TEST_ASSERT_EQUAL(900, (int)w.lowest().atUptimeSeconds);
}

void test_it_survives_the_uptime_counter_wrapping_around() {
    MemoryWatch w;
    w.update(4294967000u, 0, 150, 130, 120, 6000);
    w.update(4294967000u + 299, 0, 150, 130, 120, 6000);
    TEST_ASSERT_EQUAL(1, (int)w.size());
    w.update(4294967000u + 300, 0, 150, 130, 120, 6000);   // wraps past 2^32
    TEST_ASSERT_EQUAL(2, (int)w.size());
}

void test_negative_or_huge_readings_are_clamped() {
    MemoryWatch w;
    w.update(1, 0, -1, 5, 70000, 0);
    TEST_ASSERT_EQUAL(0, w.sample(0).freeKb);
    TEST_ASSERT_EQUAL(65535, w.sample(0).largestKb);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_sample_is_kept_at_the_first_look_and_then_every_five_minutes);
    RUN_TEST(test_it_keeps_a_day_and_drops_the_oldest);
    RUN_TEST(test_the_lowest_point_is_the_start_up_figure_until_it_drops_later);
    RUN_TEST(test_it_survives_the_uptime_counter_wrapping_around);
    RUN_TEST(test_negative_or_huge_readings_are_clamped);
    return UNITY_END();
}
