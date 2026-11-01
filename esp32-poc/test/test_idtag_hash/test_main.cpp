// Host-side tests for the ID tag fingerprint: SHA-256 against published vectors, the fingerprint against the Java app's vectors,
// and that only the fingerprint ever reaches the history records.
#include "../support/TestSupport.h"

#include <string>
#include <vector>

#include "history/HistoryService.h"
#include "ocpp/CoreHandlers.h"
#include "ocpp/IdTagHash.h"
#include "ocpp/OcppRouter.h"
#include "wallbox/ChargerRegistry.h"

using namespace solar;
using testing::FakeLink;
using testing::FakePlatform;

namespace {

std::string hex(const std::array<uint8_t, 32>& d) {
    static const char* h = "0123456789abcdef";
    std::string out;
    for (uint8_t b : d) { out += h[b >> 4]; out += h[b & 15]; }
    return out;
}

void test_sha256_matches_the_published_vectors() {
    TEST_ASSERT_TRUE(hex(Sha256::hash("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    TEST_ASSERT_TRUE(hex(Sha256::hash("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    // two blocks
    TEST_ASSERT_TRUE(hex(Sha256::hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
                     "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

void test_the_fingerprint_matches_the_java_apps_vectors() {
    TEST_ASSERT_TRUE(idTagHash("tag-1") == "35cca576");
    TEST_ASSERT_TRUE(idTagHash("abc") == "634a1613");
    TEST_ASSERT_TRUE(idTagHash("") == "");
    TEST_ASSERT_TRUE(idTagHash("   ") == "");
    TEST_ASSERT_TRUE(idTagHash("tag-1") != idTagHash("tag-2"));
}

void test_a_started_session_keeps_only_the_fingerprint() {
    FakePlatform platform;
    platform.epoch = 1760000000;
    FakeLink link;
    ChargerRegistry chargers;
    ocpp::OcppRouter router{platform};
    HistoryService history{platform};
    ocpp::registerCoreHandlers(router, platform, [](Charger&) {}, &history);
    chargers.add(1, "box", link, platform.nowMillis());
    Charger* charger = chargers.find(1);
    const std::string raw = "secret-card-123";
    router.onMessage(*charger, "[2,\"6\",\"StartTransaction\",{\"idTag\":\"" + raw + "\",\"meterStart\":100}]");
    TEST_ASSERT_TRUE(history.openSession()->tag == idTagHash(raw));
    TEST_ASSERT_TRUE(!history.openSession()->tag.empty());

    std::vector<std::string> lines;
    history.forEachLine([&](const std::string& l) { lines.push_back(l); });
    history.forEachJournalLine([&](const std::string& l) { lines.push_back(l); });
    bool rawSeen = false, tagged = false;
    for (const auto& l : lines) {
        if (l.find(raw) != std::string::npos) rawSeen = true;
        const std::string tail = "\t" + idTagHash(raw);
        if (l.rfind("O\t", 0) == 0 && l.size() >= tail.size() && l.compare(l.size() - tail.size(), tail.size(), tail) == 0) tagged = true;
    }
    for (const auto& e : history.recentEvents(50)) if (e.text.find(raw) != std::string::npos) rawSeen = true;
    TEST_ASSERT_FALSE(rawSeen);
    TEST_ASSERT_TRUE(tagged);

    HistoryService fresh{platform};
    for (const auto& l : lines) fresh.loadLine(l);
    TEST_ASSERT_TRUE(fresh.openSession()->tag == idTagHash(raw));
}

void test_a_damaged_or_old_tag_column_is_ignored() {
    FakePlatform platform;
    platform.epoch = 1760000000;
    HistoryService h{platform};
    h.loadLine("T\t100\t200\t-1.0\t-1.0\t300.0\t250.0\tRemote\t0.00\t0\t0\t0\tNOT-HEX-AT-ALL!");
    TEST_ASSERT_TRUE(h.recentSessions(1)[0].tag.empty());
    h.loadLine("T\t300\t400\t-1.0\t-1.0\t300.0\t250.0\tRemote\t0.00\t0\t0\t0");   // a file from before the column existed
    TEST_ASSERT_TRUE(h.recentSessions(2)[0].tag.empty());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sha256_matches_the_published_vectors);
    RUN_TEST(test_the_fingerprint_matches_the_java_apps_vectors);
    RUN_TEST(test_a_started_session_keeps_only_the_fingerprint);
    RUN_TEST(test_a_damaged_or_old_tag_column_is_ignored);
    return UNITY_END();
}
