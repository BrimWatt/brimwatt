// Host-side tests for saving and restoring what we know about a charger across a restart.
#include "../support/TestSupport.h"

#include "ocpp/CoreHandlers.h"
#include "ocpp/OcppRouter.h"
#include "state/ChargerPersistence.h"
#include "wallbox/ChargerRegistry.h"
#include "wallbox/WallboxController.h"

using namespace solar;
using testing::FakeLink;
using testing::FakePlatform;

namespace {

class MemoryChargerStore : public ChargerStateStore {
public:
    std::map<std::string, ChargerSnapshot> saved;
    int saves = 0;
    std::map<std::string, ChargerSnapshot> load() override { return saved; }
    void save(const std::map<std::string, ChargerSnapshot>& snapshots) override {
        saved = snapshots;
        saves++;
    }
};

struct Rig {
    FakePlatform platform;
    FakeLink link;
    ChargerRegistry chargers;
    WallboxController wallbox{platform, chargers};
    ocpp::OcppRouter router{platform};
    MemoryChargerStore store;
    ChargerPersistence persistence{platform, chargers, store};
    int changed = 0;
    Charger* charger = nullptr;

    Rig() {
        platform.epoch = 1760000000;
        ocpp::registerCoreHandlers(router, platform, [this](Charger& c) {
            changed++;
            wallbox.onChargerChanged(c);
        });
    }

    void connect() {
        chargers.add(1, "box", link, platform.nowMillis());
        charger = chargers.find(1);
    }

    void receive(const std::string& frame) { router.onMessage(*charger, frame); }

    ChargerSnapshot snapshot(ChargerStatus status, std::optional<int> txn, std::optional<int> amps, int64_t savedAt) {
        ChargerSnapshot s;
        s.status = status;
        s.transactionId = txn;
        s.appliedAmps = amps;
        s.savedAtSeconds = savedAt;
        return s;
    }
};

// ---- restoring a charger ----

void test_a_restored_charger_keeps_its_limit_for_the_draw_figure_until_it_reports() {
    Rig r;
    r.connect();
    r.charger->restoreFrom(r.snapshot(ChargerStatus::Charging, 77, 10, r.platform.epoch));
    TEST_ASSERT_TRUE(r.charger->isRestored());
    TEST_ASSERT_TRUE(r.wallbox.anyRestored());
    TEST_ASSERT_EQUAL(77, *r.charger->transactionId());
    TEST_ASSERT_FLOAT_WITHIN(0.01, 10.0, r.wallbox.currentDrawAmps());   // not 0: the dashboard shows the car's draw
    TEST_ASSERT_EQUAL(10, *r.wallbox.currentLimitAmps());

    r.receive("[2,\"1\",\"StatusNotification\",{\"connectorId\":1,\"status\":\"Charging\"}]");
    TEST_ASSERT_FALSE(r.charger->isRestored());
    TEST_ASSERT_FALSE(r.wallbox.anyRestored());
    TEST_ASSERT_EQUAL(77, *r.charger->transactionId());   // confirmed, so kept
}

void test_a_restored_transaction_is_dropped_if_the_car_has_gone() {
    for (const char* status : {"Available", "Preparing", "Finishing"}) {
        Rig r;
        r.connect();
        r.charger->restoreFrom(r.snapshot(ChargerStatus::Charging, 77, 10, r.platform.epoch));
        r.receive(std::string("[2,\"1\",\"StatusNotification\",{\"connectorId\":1,\"status\":\"") + status + "\"}]");
        TEST_ASSERT_FALSE(r.charger->transactionId().has_value());
        TEST_ASSERT_FALSE(r.charger->appliedAmps().has_value());
    }
}

void test_a_transaction_set_live_is_not_dropped_by_a_status_update() {
    Rig r;
    r.connect();
    r.charger->setStatus(ChargerStatus::Preparing);
    r.charger->setTransactionId(5);
    r.receive("[2,\"1\",\"StatusNotification\",{\"connectorId\":1,\"status\":\"Preparing\"}]");
    TEST_ASSERT_EQUAL(5, *r.charger->transactionId());
}

void test_a_restored_charge_can_be_changed_at_once_in_custom_mode() {
    Rig r;
    r.connect();
    r.charger->restoreFrom(r.snapshot(ChargerStatus::Charging, 77, 10, r.platform.epoch));
    r.wallbox.holdFixedLimit(16);
    TEST_ASSERT_EQUAL(1, r.link.count("\"SetChargingProfile\""));
    TEST_ASSERT_TRUE(testing::contains(r.link.last(), "\"transactionId\":77"));
}

void test_learning_the_transaction_from_meter_values_applies_a_held_limit() {
    Rig r;
    r.connect();
    r.charger->setStatus(ChargerStatus::Charging);          // found charging, but nothing is known about it
    r.wallbox.holdFixedLimit(16);
    TEST_ASSERT_EQUAL(0, r.link.count("\"SetChargingProfile\""));   // no transaction yet, so nothing can be sent

    r.receive("[2,\"2\",\"MeterValues\",{\"connectorId\":1,\"transactionId\":42,\"meterValue\":[{\"sampledValue\":["
              "{\"measurand\":\"Energy.Active.Import.Register\",\"value\":\"1\",\"unit\":\"Wh\"}]}]}]");
    TEST_ASSERT_EQUAL(42, *r.charger->transactionId());
    TEST_ASSERT_EQUAL(1, r.link.count("\"SetChargingProfile\""));   // now it can
}

// ---- charging sessions come from the wallbox's own start and stop messages ----

void test_a_start_and_stop_transaction_make_a_session() {
    Rig r;
    HistoryService history(r.platform);
    ocpp::OcppRouter router(r.platform);
    ocpp::registerCoreHandlers(router, r.platform, [](Charger&) {}, &history);
    r.connect();
    r.platform.epoch += 60;
    router.onMessage(*r.charger, "[2,\"1\",\"StartTransaction\",{\"connectorId\":1,\"meterStart\":5896666}]");
    TEST_ASSERT_NOT_NULL(history.openSession());
    TEST_ASSERT_FLOAT_WITHIN(0.1, 5896666, history.openSession()->meterStartWh);
    r.platform.epoch += 3600;
    router.onMessage(*r.charger, "[2,\"2\",\"StopTransaction\",{\"meterStop\":5899066,\"reason\":\"Remote\",\"transactionId\":1}]");
    TEST_ASSERT_NULL(history.openSession());
    auto done = history.recentSessions(5);
    TEST_ASSERT_EQUAL(1, done.size());
    TEST_ASSERT_FLOAT_WITHIN(0.5, 2400, done[0].energyWh());
    testing::check("Remote", done[0].reason);
}

// ---- saving and recalling ----

void test_a_charger_is_saved_on_the_first_tick_and_only_again_when_it_changes() {
    Rig r;
    r.connect();
    r.charger->setStatus(ChargerStatus::Charging);
    r.charger->setTransactionId(9);
    r.charger->setAppliedAmps(12);

    r.persistence.tick();
    TEST_ASSERT_EQUAL(1, r.store.saves);
    TEST_ASSERT_EQUAL(9, *r.store.saved["box"].transactionId);
    TEST_ASSERT_EQUAL(12, *r.store.saved["box"].appliedAmps);

    r.platform.now += 6000;
    r.persistence.tick();
    TEST_ASSERT_EQUAL(1, r.store.saves);   // nothing changed

    r.charger->setAppliedAmps(14);
    r.platform.now += 1000;
    r.persistence.tick();
    TEST_ASSERT_EQUAL(1, r.store.saves);   // too soon after the last check

    r.platform.now += 6000;
    r.persistence.tick();
    TEST_ASSERT_EQUAL(2, r.store.saves);
    TEST_ASSERT_EQUAL(14, *r.store.saved["box"].appliedAmps);
}

void test_the_timestamp_is_refreshed_every_five_minutes_even_without_changes() {
    Rig r;
    r.connect();
    r.charger->setStatus(ChargerStatus::Charging);
    r.persistence.tick();
    TEST_ASSERT_EQUAL(1, r.store.saves);

    r.platform.now += 6000;
    r.platform.epoch += 299;
    r.persistence.tick();
    TEST_ASSERT_EQUAL(1, r.store.saves);

    r.platform.now += 6000;
    r.platform.epoch += 2;
    r.persistence.tick();
    TEST_ASSERT_EQUAL(2, r.store.saves);
    TEST_ASSERT_EQUAL(r.platform.epoch, r.store.saved["box"].savedAtSeconds);
}

void test_the_state_is_captured_the_moment_the_connection_goes() {
    Rig r;
    r.connect();
    r.charger->setStatus(ChargerStatus::Charging);
    r.charger->setTransactionId(9);
    r.persistence.onDisconnect(*r.charger);
    TEST_ASSERT_EQUAL(1, r.store.saves);
    TEST_ASSERT_EQUAL(9, *r.store.saved["box"].transactionId);
}

void test_saved_state_is_recalled_after_a_restart_if_recent_enough() {
    Rig first;
    first.connect();
    first.charger->setStatus(ChargerStatus::Charging);
    first.charger->setTransactionId(9);
    first.charger->setAppliedAmps(10);
    first.persistence.tick();

    Rig second;
    second.store.saved = first.store.saved;
    second.persistence.restore();

    second.platform.epoch += 600;   // ten minutes later
    auto recalled = second.persistence.recall("box");
    TEST_ASSERT_TRUE(recalled.has_value());
    TEST_ASSERT_EQUAL(static_cast<int>(ChargerStatus::Charging), static_cast<int>(recalled->status));
    TEST_ASSERT_EQUAL(9, *recalled->transactionId);
    TEST_ASSERT_EQUAL(10, *recalled->appliedAmps);

    TEST_ASSERT_FALSE(second.persistence.recall("other").has_value());

    second.platform.epoch += 3600;  // now older than an hour
    TEST_ASSERT_FALSE(second.persistence.recall("box").has_value());
}

void test_two_chargers_are_kept_apart() {
    Rig r;
    FakeLink other;
    r.chargers.add(1, "a", r.link, r.platform.nowMillis());
    r.chargers.add(2, "b", other, r.platform.nowMillis());
    r.chargers.find(1)->setStatus(ChargerStatus::Charging);
    r.chargers.find(2)->setStatus(ChargerStatus::Available);
    r.persistence.tick();
    TEST_ASSERT_EQUAL(static_cast<int>(ChargerStatus::Charging), static_cast<int>(r.store.saved["a"].status));
    TEST_ASSERT_EQUAL(static_cast<int>(ChargerStatus::Available), static_cast<int>(r.store.saved["b"].status));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_restored_charger_keeps_its_limit_for_the_draw_figure_until_it_reports);
    RUN_TEST(test_a_restored_transaction_is_dropped_if_the_car_has_gone);
    RUN_TEST(test_a_transaction_set_live_is_not_dropped_by_a_status_update);
    RUN_TEST(test_a_restored_charge_can_be_changed_at_once_in_custom_mode);
    RUN_TEST(test_learning_the_transaction_from_meter_values_applies_a_held_limit);
    RUN_TEST(test_a_start_and_stop_transaction_make_a_session);
    RUN_TEST(test_a_charger_is_saved_on_the_first_tick_and_only_again_when_it_changes);
    RUN_TEST(test_the_timestamp_is_refreshed_every_five_minutes_even_without_changes);
    RUN_TEST(test_the_state_is_captured_the_moment_the_connection_goes);
    RUN_TEST(test_saved_state_is_recalled_after_a_restart_if_recent_enough);
    RUN_TEST(test_two_chargers_are_kept_apart);
    return UNITY_END();
}
