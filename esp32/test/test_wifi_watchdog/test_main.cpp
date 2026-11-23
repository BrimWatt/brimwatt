// Host-side tests for the Wi-Fi watchdog: how long it waits, what it asks for while the router is gone, and that it notices the return.
#include "../support/TestSupport.h"

#include "net/WifiWatchdog.h"

using solar::WifiWatchdog;
using Step = WifiWatchdog::Step;

namespace {

void test_nothing_happens_while_connected() {
    WifiWatchdog w;
    for (uint32_t t = 0; t < 120000; t += 1000) TEST_ASSERT_TRUE(w.update(true, t) == Step::None);
    TEST_ASSERT_FALSE(w.isDown());
    TEST_ASSERT_FALSE(w.takeRecovered());
}

void test_the_driver_gets_a_grace_period_first() {
    WifiWatchdog w;
    TEST_ASSERT_TRUE(w.update(false, 1000) == Step::None);   // lost
    TEST_ASSERT_TRUE(w.update(false, 15999) == Step::None);
    TEST_ASSERT_TRUE(w.update(false, 16000) == Step::Reconnect);
}

void test_then_fresh_joins_every_minute_with_a_radio_reset_every_fifth() {
    WifiWatchdog w;
    uint32_t t = 0;
    w.update(false, t);
    t += 15000;
    TEST_ASSERT_TRUE(w.update(false, t) == Step::Reconnect);
    t += 14999;
    TEST_ASSERT_TRUE(w.update(false, t) == Step::None);
    t += 1;
    for (int i = 1; i <= 10; i++) {
        TEST_ASSERT_TRUE(w.update(false, t) == (i % 5 == 0 ? Step::ResetRadio : Step::Rejoin));
        TEST_ASSERT_TRUE(w.update(false, t + 59999) == Step::None);
        t += 60000;
    }
}

void test_the_return_is_noticed_once_with_the_outage_length() {
    WifiWatchdog w;
    w.update(false, 5000);
    w.update(false, 70000);
    TEST_ASSERT_TRUE(w.isDown());
    TEST_ASSERT_TRUE(w.update(true, 125000) == Step::None);
    TEST_ASSERT_FALSE(w.isDown());
    TEST_ASSERT_TRUE(w.takeRecovered());
    TEST_ASSERT_EQUAL_UINT32(120, w.outageSeconds());
    TEST_ASSERT_FALSE(w.takeRecovered());
}

void test_a_second_outage_starts_again_with_the_grace_period() {
    WifiWatchdog w;
    w.update(false, 0);
    w.update(false, 20000);   // reconnect
    w.update(true, 30000);
    w.update(false, 40000);
    TEST_ASSERT_TRUE(w.update(false, 54999) == Step::None);
    TEST_ASSERT_TRUE(w.update(false, 55000) == Step::Reconnect);
}

void test_it_survives_the_clock_wrapping_around() {
    WifiWatchdog w;
    uint32_t t = 0xFFFFFFFFu - 5000;
    w.update(false, t);
    TEST_ASSERT_TRUE(w.update(false, t + 14000) == Step::None);
    TEST_ASSERT_TRUE(w.update(false, t + 15000) == Step::Reconnect);   // wraps past zero
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_nothing_happens_while_connected);
    RUN_TEST(test_the_driver_gets_a_grace_period_first);
    RUN_TEST(test_then_fresh_joins_every_minute_with_a_radio_reset_every_fifth);
    RUN_TEST(test_the_return_is_noticed_once_with_the_outage_length);
    RUN_TEST(test_a_second_outage_starts_again_with_the_grace_period);
    RUN_TEST(test_it_survives_the_clock_wrapping_around);
    return UNITY_END();
}
