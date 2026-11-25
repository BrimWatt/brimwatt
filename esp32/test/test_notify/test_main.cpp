// Host-side tests for notifications: which events are sent, the repeat limit, address checking and masking.
#include "../support/TestSupport.h"

#include "notify/Notifier.h"

using namespace solar;
using testing::FakePlatform;

namespace {

class FakeSender : public NotifySender {
public:
    std::vector<std::pair<std::string, std::string>> sent;
    std::string answer;   // what sendNow reports
    void sendAsync(const std::string& url, const std::string& json) override { sent.push_back({url, json}); }
    std::string sendNow(const std::string& url, const std::string& json) override {
        sent.push_back({url, json});
        return answer;
    }
};

struct Rig {
    FakePlatform platform;
    SystemState state;
    FakeSender sender;
    Notifier notifier{platform, state, sender};
};

void test_nothing_is_sent_until_an_address_is_set() {
    Rig r;
    r.notifier.onEvent("Charging ended: 12.0 kWh");
    TEST_ASSERT_EQUAL(0, r.sender.sent.size());
    testing::check("No address set", r.notifier.sendTest());
}

void test_only_the_important_events_are_sent() {
    Rig r;
    r.state.setNotifyUrl("https://example.com/hook/secret");
    r.notifier.onEvent("Mode set to Eco");
    r.notifier.onEvent("Grid allowance set to 500 W");
    TEST_ASSERT_EQUAL(0, r.sender.sent.size());
    r.notifier.onEvent("Charging ended: 12.0 kWh, 80% from the sun");
    r.notifier.onEvent("Lost contact with the solar meter: charging stopped");
    TEST_ASSERT_EQUAL(2, r.sender.sent.size());
    testing::check("https://example.com/hook/secret", r.sender.sent[0].first);
    testing::check("{\"text\":\"Brimwatt: Charging ended: 12.0 kWh, 80% from the sun\",\"content\":\"Brimwatt: Charging ended: 12.0 kWh, 80% from the sun\"}",
                   r.sender.sent[0].second);
}

void test_the_same_message_is_not_repeated_within_ten_minutes() {
    Rig r;
    r.state.setNotifyUrl("https://example.com/hook");
    const char* text = "Lost contact with the solar meter: charging stopped";
    r.notifier.onEvent(text);
    r.platform.now += 9 * 60 * 1000;
    r.notifier.onEvent(text);
    TEST_ASSERT_EQUAL(1, r.sender.sent.size());
    r.platform.now += 2 * 60 * 1000;
    r.notifier.onEvent(text);
    TEST_ASSERT_EQUAL(2, r.sender.sent.size());
}

void test_the_test_button_reports_what_went_wrong() {
    Rig r;
    r.state.setNotifyUrl("https://example.com/hook");
    testing::check("", r.notifier.sendTest());
    r.sender.answer = "the server answered 404";
    testing::check("the server answered 404", r.notifier.sendTest());
}

void test_addresses_are_checked() {
    TEST_ASSERT_TRUE(Notifier::isValidUrl(""));
    TEST_ASSERT_TRUE(Notifier::isValidUrl("https://hooks.example.com/a/b"));
    TEST_ASSERT_TRUE(Notifier::isValidUrl("http://192.168.1.5:8123/api/webhook/x"));
    for (const char* bad : {"ftp://x.com/a", "not a url", "https://", "https:///path", "https://x.com/a b", "https://x.com/\"q"}) {
        TEST_ASSERT_FALSE(Notifier::isValidUrl(bad));
    }
    TEST_ASSERT_FALSE(Notifier::isValidUrl("https://x.com/" + std::string(200, 'a')));
}

void test_only_the_host_is_shown_back() {
    testing::check("https://hooks.example.com/…", Notifier::maskedHost("https://hooks.example.com/services/T000/B000/SECRET"));
    testing::check("http://192.168.1.5:8123/…", Notifier::maskedHost("http://192.168.1.5:8123/api/webhook/x"));
    testing::check("", Notifier::maskedHost(""));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_nothing_is_sent_until_an_address_is_set);
    RUN_TEST(test_only_the_important_events_are_sent);
    RUN_TEST(test_the_same_message_is_not_repeated_within_ten_minutes);
    RUN_TEST(test_the_test_button_reports_what_went_wrong);
    RUN_TEST(test_addresses_are_checked);
    RUN_TEST(test_only_the_host_is_shown_back);
    return UNITY_END();
}
