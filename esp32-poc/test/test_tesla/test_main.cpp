// Host-side tests for the Tesla Bluetooth scan helpers: the VIN rule, the car's advertised name and the signal verdict.
#include "../support/TestSupport.h"

#include "tesla/TeslaBle.h"

using namespace solar::tesla;

void setUp() {}
void tearDown() {}

void test_vin_rules() {
    TEST_ASSERT_TRUE(isValidVin("5YJS0000000000000"));
    TEST_ASSERT_TRUE(isValidVin("5yjs0000000000000"));   // case is ignored
    TEST_ASSERT_FALSE(isValidVin("5YJS000000000000"));   // 16
    TEST_ASSERT_FALSE(isValidVin("5YJS00000000000000")); // 18
    TEST_ASSERT_FALSE(isValidVin("5YJS000000000000I"));  // I, O and Q never occur
    TEST_ASSERT_FALSE(isValidVin("5YJS000000000000O"));
    TEST_ASSERT_FALSE(isValidVin("5YJS000000000000Q"));
    TEST_ASSERT_FALSE(isValidVin("5YJS00000000000 0"));
    TEST_ASSERT_FALSE(isValidVin(""));
}

void test_sha1_known_answers() {
    auto hex = [](const std::string& s) {
        std::string out;
        char b[3];
        for (uint8_t x : sha1(s)) { snprintf(b, sizeof b, "%02x", x); out += b; }
        return out;
    };
    TEST_ASSERT_EQUAL_STRING("da39a3ee5e6b4b0d3255bfef95601890afd80709", hex("").c_str());
    TEST_ASSERT_EQUAL_STRING("a9993e364706816aba3e25717850c26c9cd0d89d", hex("abc").c_str());
    // two blocks
    TEST_ASSERT_EQUAL_STRING("84983e441c3bd26ebaae4aa1f95129e5e54670f1",
                             hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").c_str());
}

void test_the_advertised_name_matches_teslas_own_example() {
    // The example in Tesla's published protocol description.
    TEST_ASSERT_EQUAL_STRING("S1a87a5a75f3df858C", advertisedName("5YJS0000000000000").c_str());
    TEST_ASSERT_EQUAL_STRING("S1a87a5a75f3df858C", advertisedName("5yjs0000000000000").c_str());
    TEST_ASSERT_EQUAL_STRING("", advertisedName("not a vin").c_str());
}

void test_the_vin_is_masked() {
    TEST_ASSERT_EQUAL_STRING("5YJ**********0000", maskVin("5YJS0000000000000").c_str());
    TEST_ASSERT_EQUAL_STRING("(invalid)", maskVin("short").c_str());
}

void test_nothing_heard() {
    ScanStats s;
    s.start(1000, 60000);
    TEST_ASSERT_EQUAL_STRING("Not heard", s.verdict());
    TEST_ASSERT_EQUAL(0, s.heardPercent());
    TEST_ASSERT_TRUE(s.summary().find("Not heard") == 0);
}

void test_a_strong_steady_signal_is_good() {
    ScanStats s;
    s.start(0, 60000);
    for (uint32_t t = 0; t < 60000; t += 1000) s.heard(t, -60);
    TEST_ASSERT_EQUAL(100, s.heardPercent());
    TEST_ASSERT_EQUAL(-60, s.average());
    TEST_ASSERT_EQUAL_STRING("Good", s.verdict());
}

void test_a_patchy_signal_is_weak() {
    ScanStats s;
    s.start(0, 60000);
    for (uint32_t t = 0; t < 60000; t += 20000) s.heard(t, -90);   // 3 of 12 windows
    TEST_ASSERT_EQUAL(25, s.heardPercent());
    TEST_ASSERT_EQUAL_STRING("Weak", s.verdict());
}

void test_a_loud_but_gappy_signal_is_not_good() {
    ScanStats s;
    s.start(0, 60000);
    for (uint32_t t = 0; t < 60000; t += 10000) s.heard(t, -55);   // 6 of 12 windows
    TEST_ASSERT_EQUAL(50, s.heardPercent());
    TEST_ASSERT_EQUAL_STRING("Weak", s.verdict());
}

void test_min_max_average_and_late_sightings() {
    ScanStats s;
    s.start(10000, 10000);
    s.heard(5000, -10);    // before the scan: ignored
    s.heard(10000, -70);
    s.heard(16000, -80);
    s.heard(30000, -20);   // after the scan: ignored
    TEST_ASSERT_EQUAL(2, s.sightings());
    TEST_ASSERT_EQUAL(-75, s.average());
    TEST_ASSERT_EQUAL(-70, s.strongest());
    TEST_ASSERT_EQUAL(-80, s.weakest());
    TEST_ASSERT_EQUAL(100, s.heardPercent());   // both windows
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_vin_rules);
    RUN_TEST(test_sha1_known_answers);
    RUN_TEST(test_the_advertised_name_matches_teslas_own_example);
    RUN_TEST(test_the_vin_is_masked);
    RUN_TEST(test_nothing_heard);
    RUN_TEST(test_a_strong_steady_signal_is_good);
    RUN_TEST(test_a_patchy_signal_is_weak);
    RUN_TEST(test_a_loud_but_gappy_signal_is_not_good);
    RUN_TEST(test_min_max_average_and_late_sightings);
    return UNITY_END();
}
