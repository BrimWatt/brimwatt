// Host-side tests for the site: the time zone rules, and what a saved location is checked against.
#include "../support/TestSupport.h"

#include "site/Site.h"

using namespace solar;

namespace {

void test_us_zones_have_a_clock_rule_with_daylight_saving() {
    TEST_ASSERT_TRUE(*timezones::posixFor("America/Los_Angeles") == "PST8PDT,M3.2.0,M11.1.0");
    TEST_ASSERT_TRUE(*timezones::posixFor("America/Denver") == "MST7MDT,M3.2.0,M11.1.0");
    TEST_ASSERT_TRUE(*timezones::posixFor("America/Chicago") == "CST6CDT,M3.2.0,M11.1.0");
    TEST_ASSERT_TRUE(*timezones::posixFor("America/New_York") == "EST5EDT,M3.2.0,M11.1.0");
    TEST_ASSERT_TRUE(*timezones::posixFor("America/Anchorage") == "AKST9AKDT,M3.2.0,M11.1.0");
    TEST_ASSERT_TRUE(*timezones::posixFor("America/Indiana/Indianapolis") == "EST5EDT,M3.2.0,M11.1.0");
}

void test_arizona_and_hawaii_do_not_change_their_clocks() {
    TEST_ASSERT_TRUE(*timezones::posixFor("America/Phoenix") == "MST7");
    TEST_ASSERT_TRUE(*timezones::posixFor("Pacific/Honolulu") == "HST10");
}

void test_an_unknown_zone_is_refused_not_guessed() {
    TEST_ASSERT_FALSE(timezones::posixFor("Europe/Paris").has_value());
    TEST_ASSERT_FALSE(timezones::posixFor("").has_value());
    TEST_ASSERT_FALSE(timezones::posixFor("america/los_angeles").has_value());
}

void test_the_built_in_default_is_a_known_zone() {
    Site defaults;
    TEST_ASSERT_TRUE(timezones::posixFor(defaults.timezone).has_value());
}

void test_decimals_must_be_whole_numbers_only() {
    TEST_ASSERT_TRUE(*parseDecimal("37.84") == 37.84);
    TEST_ASSERT_TRUE(*parseDecimal("-121.3") == -121.3);
    TEST_ASSERT_FALSE(parseDecimal("").has_value());
    TEST_ASSERT_FALSE(parseDecimal("12abc").has_value());
    TEST_ASSERT_FALSE(parseDecimal("nan").has_value());
    TEST_ASSERT_FALSE(parseDecimal("inf").has_value());
}

void test_a_good_site_is_rounded_to_two_decimals() {
    std::string error;
    auto s = parseSite("34.0901", "-118.4065", "America/Los_Angeles", "Beverly Hills, California", error);
    TEST_ASSERT_TRUE(s.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 34.09, s->latitude);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, -118.41, s->longitude);
    TEST_ASSERT_TRUE(s->name == "Beverly Hills, California");
}

void test_the_edges_are_allowed_and_beyond_them_are_not() {
    std::string error;
    TEST_ASSERT_TRUE(parseSite("90", "180", "America/Denver", "", error).has_value());
    TEST_ASSERT_TRUE(parseSite("-90", "-180", "America/Denver", "", error).has_value());
    TEST_ASSERT_FALSE(parseSite("90.01", "0", "America/Denver", "", error).has_value());
    TEST_ASSERT_TRUE(error.find("Latitude") != std::string::npos);
    TEST_ASSERT_FALSE(parseSite("0", "-180.5", "America/Denver", "", error).has_value());
    TEST_ASSERT_TRUE(error.find("Longitude") != std::string::npos);
}

void test_names_are_short_and_printable() {
    std::string error;
    TEST_ASSERT_TRUE(parseSite("1", "2", "America/Denver", std::string(60, 'a'), error).has_value());
    TEST_ASSERT_FALSE(parseSite("1", "2", "America/Denver", std::string(61, 'a'), error).has_value());
    TEST_ASSERT_FALSE(parseSite("1", "2", "America/Denver", "tab\there", error).has_value());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_us_zones_have_a_clock_rule_with_daylight_saving);
    RUN_TEST(test_arizona_and_hawaii_do_not_change_their_clocks);
    RUN_TEST(test_an_unknown_zone_is_refused_not_guessed);
    RUN_TEST(test_the_built_in_default_is_a_known_zone);
    RUN_TEST(test_decimals_must_be_whole_numbers_only);
    RUN_TEST(test_a_good_site_is_rounded_to_two_decimals);
    RUN_TEST(test_the_edges_are_allowed_and_beyond_them_are_not);
    RUN_TEST(test_names_are_short_and_printable);
    return UNITY_END();
}
