// Host-side tests for the portable core: OCPP frames, routing, handlers, the wallbox controller.
#include "../support/TestSupport.h"

#include <string>
#include <vector>

#include "common/Config.h"
#include "common/Scheduler.h"
#include "common/Uuid.h"
#include "ocpp/ChargerId.h"
#include "ocpp/CoreHandlers.h"
#include "ocpp/OcppMessages.h"
#include "ocpp/OcppRouter.h"
#include "wallbox/ChargerRegistry.h"
#include "wallbox/WallboxController.h"

using namespace solar;

namespace {

using testing::FakeLink;
using testing::FakePlatform;
using testing::check;
using testing::contains;

struct Rig {
    FakePlatform platform;
    FakeLink link;
    ChargerRegistry chargers;
    ocpp::OcppRouter router{platform};
    WallboxController wallbox{platform, chargers};
    int changed = 0;
    Charger* charger;

    Rig() {
        ocpp::registerCoreHandlers(router, platform, [this](Charger& c) {
            changed++;
            wallbox.onChargerChanged(c);
        });
        chargers.add(1, "box", link, platform.nowMillis());
        charger = chargers.find(1);
    }

    void receive(const std::string& frame) { router.onMessage(*charger, frame); }
    void status(const char* s) {
        receive(std::string("[2,\"s\",\"StatusNotification\",{\"connectorId\":1,\"status\":\"") + s + "\"}]");
        link.sent.clear();
    }
};

// ---- frame builders ----

void test_call_result_uses_the_four_element_layout() {
    JsonDocument payload;
    payload["currentTime"] = "T";
    check("[3,\"abc\",\"Heartbeat\",{\"currentTime\":\"T\"}]", ocpp::callResult("abc", "Heartbeat", payload));
}

void test_call_error() {
    check("[4,\"abc\",\"NotImplemented\",\"X is not supported\",{}]",
          ocpp::callError("abc", "NotImplemented", "X is not supported"));
}

void test_set_charging_profile_matches_the_java_frame() {
    check("[2,\"m1\",\"SetChargingProfile\",{\"connectorId\":1,\"csChargingProfiles\":{\"chargingProfileId\":0,"
          "\"stackLevel\":1,\"chargingProfileKind\":\"Absolute\",\"chargingProfilePurpose\":\"TxProfile\","
          "\"chargingSchedule\":{\"chargingRateUnit\":\"A\",\"minChargingRate\":16,"
          "\"chargingSchedulePeriod\":[{\"limit\":16,\"startPeriod\":0}]},\"transactionId\":7}}]",
          ocpp::setChargingProfile("m1", 7, 16, "A"));
}

void test_remote_start_embeds_the_profile() {
    check("[2,\"m2\",\"RemoteStartTransaction\",{\"idTag\":\"tag\",\"connectorId\":1,\"chargingProfile\":{"
          "\"chargingProfileId\":5,\"stackLevel\":1,\"chargingProfileKind\":\"Absolute\","
          "\"chargingProfilePurpose\":\"TxProfile\",\"chargingSchedule\":{\"chargingRateUnit\":\"A\","
          "\"minChargingRate\":10,\"chargingSchedulePeriod\":[{\"limit\":10,\"startPeriod\":0}]}}}]",
          ocpp::remoteStartTransaction("m2", "tag", 5, 10, "A"));
}

void test_remote_stop_and_trigger() {
    check("[2,\"m3\",\"RemoteStopTransaction\",{\"transactionId\":7}]", ocpp::remoteStopTransaction("m3", 7));
    check("[2,\"m4\",\"TriggerMessage\",{\"requestedMessage\":\"MeterValues\",\"connectorId\":1}]",
          ocpp::triggerMessage("m4", "MeterValues"));
}

void test_uuid_has_the_standard_layout() {
    FakePlatform p;
    std::string id = newUuid(p);
    TEST_ASSERT_EQUAL(36, id.size());
    TEST_ASSERT_EQUAL('-', id[8]);
    TEST_ASSERT_EQUAL('4', id[14]);
}

// ---- router and handlers ----

void test_boot_notification_is_accepted_with_the_heartbeat_interval() {
    Rig r;
    r.receive("[2,\"1\",\"BootNotification\",{\"chargePointVendor\":\"x\"}]");
    check("[3,\"1\",\"BootNotification\",{\"status\":\"Accepted\",\"currentTime\":\"2026-10-14T22:00:00.000Z\","
          "\"interval\":60}]", r.link.last());
}

void test_heartbeat_returns_the_time() {
    Rig r;
    r.receive("[2,\"2\",\"Heartbeat\",{}]");
    check("[3,\"2\",\"Heartbeat\",{\"currentTime\":\"2026-10-14T22:00:00.000Z\"}]", r.link.last());
}

void test_unknown_action_gets_not_implemented() {
    Rig r;
    r.receive("[2,\"3\",\"DataTransfer\",{}]");
    TEST_ASSERT_TRUE(contains(r.link.last(), "\"NotImplemented\""));
    TEST_ASSERT_TRUE(contains(r.link.last(), "[4,\"3\""));
}

void test_garbage_is_ignored() {
    Rig r;
    r.receive("not json");
    r.receive("[2]");
    r.receive("{}");
    r.receive("[9,\"x\",\"y\"]");
    TEST_ASSERT_EQUAL(0, r.link.sent.size());
}

void test_any_message_counts_as_a_sign_of_life() {
    Rig r;
    r.platform.now = 250000;
    r.receive("[2,\"2\",\"Heartbeat\",{}]");
    TEST_ASSERT_EQUAL(250000, r.charger->lastSeenMillis());
}

void test_status_notification_updates_the_charger_and_ignores_connector_zero() {
    Rig r;
    r.receive("[2,\"4\",\"StatusNotification\",{\"connectorId\":0,\"status\":\"Faulted\"}]");
    TEST_ASSERT_EQUAL(ChargerStatus::Unknown, r.charger->status());
    TEST_ASSERT_EQUAL(0, r.changed);

    r.receive("[2,\"5\",\"StatusNotification\",{\"connectorId\":1,\"status\":\"Preparing\"}]");
    TEST_ASSERT_EQUAL(ChargerStatus::Preparing, r.charger->status());
    TEST_ASSERT_EQUAL(1, r.changed);
    check("[3,\"5\",\"StatusNotification\",{}]", r.link.last());
}

void test_start_transaction_assigns_increasing_ids_seeded_from_the_clock() {
    Rig r;
    r.receive("[2,\"6\",\"StartTransaction\",{}]");
    TEST_ASSERT_EQUAL(1001, *r.charger->transactionId());
    TEST_ASSERT_TRUE(contains(r.link.last(), "\"transactionId\":1001"));
    r.receive("[2,\"7\",\"StartTransaction\",{}]");
    TEST_ASSERT_EQUAL(1002, *r.charger->transactionId());
}

void test_stop_transaction_clears_the_transaction_and_limit() {
    Rig r;
    r.charger->setTransactionId(5);
    r.charger->setAppliedAmps(12);
    r.receive("[2,\"8\",\"StopTransaction\",{}]");
    TEST_ASSERT_FALSE(r.charger->transactionId().has_value());
    TEST_ASSERT_FALSE(r.charger->appliedAmps().has_value());
}

void test_meter_values_current_is_recorded_and_the_transaction_learned() {
    Rig r;
    r.receive("[2,\"9\",\"MeterValues\",{\"connectorId\":1,\"transactionId\":42,\"meterValue\":[{\"sampledValue\":["
              "{\"measurand\":\"Current.Import\",\"value\":\"11.8\",\"unit\":\"A\"}]}]}]");
    TEST_ASSERT_EQUAL(42, *r.charger->transactionId());
    auto amps = r.charger->freshMeasuredAmps(r.platform.now, 1000);
    TEST_ASSERT_TRUE(amps.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01, 11.8, *amps);
    TEST_ASSERT_EQUAL(ChargerStatus::Charging, r.charger->status());   // delivering current, so charging
}

void test_meter_values_power_is_converted_to_amps() {
    Rig r;
    r.receive("[2,\"9\",\"MeterValues\",{\"meterValue\":[{\"sampledValue\":["
              "{\"measurand\":\"Power.Active.Import\",\"value\":\"2.4\",\"unit\":\"kW\"}]}]}]");
    TEST_ASSERT_FLOAT_WITHIN(0.01, 10.0, *r.charger->freshMeasuredAmps(r.platform.now, 1000));
}

void test_a_measurement_goes_stale() {
    Rig r;
    r.charger->setMeasuredAmps(9, r.platform.now);
    TEST_ASSERT_TRUE(r.charger->freshMeasuredAmps(r.platform.now + 500, 1000).has_value());
    TEST_ASSERT_FALSE(r.charger->freshMeasuredAmps(r.platform.now + 5000, 1000).has_value());
}

void test_a_refused_meter_request_is_remembered() {
    Rig r;
    r.status("Charging");
    r.wallbox.refreshMeasurements();
    TEST_ASSERT_EQUAL(1, r.link.sent.size());
    std::string id = std::string(r.link.last()).substr(4, 36);
    r.receive("[4,\"" + id + "\",\"NotSupported\",\"no\",{}]");
    TEST_ASSERT_TRUE(r.charger->meterTriggerUnsupported());

    r.link.sent.clear();
    r.wallbox.refreshMeasurements();
    TEST_ASSERT_EQUAL(0, r.link.sent.size());   // it is not asked again
}

void test_a_rejected_meter_request_is_remembered() {
    Rig r;
    r.status("Charging");
    r.wallbox.refreshMeasurements();
    std::string id = std::string(r.link.last()).substr(4, 36);
    r.receive("[3,\"" + id + "\",{\"status\":\"Rejected\"}]");
    TEST_ASSERT_TRUE(r.charger->meterTriggerUnsupported());
}

void test_an_accepted_meter_request_keeps_asking() {
    Rig r;
    r.status("Charging");
    r.wallbox.refreshMeasurements();
    std::string id = std::string(r.link.last()).substr(4, 36);
    r.receive("[3,\"" + id + "\",{\"status\":\"Accepted\"}]");
    TEST_ASSERT_FALSE(r.charger->meterTriggerUnsupported());
}

// ---- wallbox controller ----

void test_a_waiting_car_is_started_remotely_with_the_limit() {
    Rig r;
    r.status("Preparing");
    r.wallbox.sendChargingLimit(16);
    TEST_ASSERT_EQUAL(1, r.link.sent.size());
    TEST_ASSERT_TRUE(contains(r.link.last(), "\"RemoteStartTransaction\""));
    TEST_ASSERT_TRUE(contains(r.link.last(), "\"limit\":16"));
    TEST_ASSERT_EQUAL(16, *r.charger->appliedAmps());
}

void test_remote_start_is_not_repeated_within_thirty_seconds() {
    Rig r;
    r.status("Preparing");
    r.wallbox.sendChargingLimit(16);
    r.platform.now += 10000;
    r.wallbox.sendChargingLimit(16);
    TEST_ASSERT_EQUAL(1, r.link.sent.size());
    r.platform.now += 25000;
    r.wallbox.sendChargingLimit(16);
    TEST_ASSERT_EQUAL(2, r.link.sent.size());
}

void test_the_first_start_is_not_blocked_right_after_boot() {
    Rig r;
    r.platform.now = 3000;   // three seconds after power-up
    r.status("Preparing");
    r.wallbox.sendChargingLimit(10);
    TEST_ASSERT_EQUAL(1, r.link.sent.size());
}

void test_a_charging_car_gets_a_profile_only_when_the_limit_changes() {
    Rig r;
    r.status("Charging");
    r.charger->setTransactionId(7);
    r.wallbox.sendChargingLimit(12);
    TEST_ASSERT_EQUAL(1, r.link.sent.size());
    TEST_ASSERT_TRUE(contains(r.link.last(), "\"SetChargingProfile\""));
    TEST_ASSERT_TRUE(contains(r.link.last(), "\"transactionId\":7"));
    r.wallbox.sendChargingLimit(12);
    TEST_ASSERT_EQUAL(1, r.link.sent.size());
    r.wallbox.sendChargingLimit(14);
    TEST_ASSERT_EQUAL(2, r.link.sent.size());
}

void test_nothing_is_sent_without_a_known_transaction() {
    Rig r;
    r.status("Charging");
    r.wallbox.sendChargingLimit(12);
    r.wallbox.sendChargingLimit(0);
    TEST_ASSERT_EQUAL(0, r.link.sent.size());
}

void test_zero_stops_a_charging_car_and_is_ignored_otherwise() {
    Rig r;
    r.status("Preparing");
    r.wallbox.sendChargingLimit(0);
    TEST_ASSERT_EQUAL(0, r.link.sent.size());

    r.status("Charging");
    r.charger->setTransactionId(7);
    r.wallbox.sendChargingLimit(0);
    TEST_ASSERT_EQUAL(1, r.link.sent.size());
    check("RemoteStopTransaction", std::string(r.link.last()).substr(43, 21));
}

void test_a_closed_connection_gets_nothing() {
    Rig r;
    r.status("Preparing");
    r.link.open = false;
    r.wallbox.sendChargingLimit(10);
    TEST_ASSERT_EQUAL(0, r.link.sent.size());
}

void test_a_held_limit_starts_a_car_plugged_in_later() {
    Rig r;
    r.wallbox.holdFixedLimit(10);
    TEST_ASSERT_EQUAL(0, r.link.sent.size());   // nothing is plugged in yet
    r.receive("[2,\"1\",\"StatusNotification\",{\"connectorId\":1,\"status\":\"Preparing\"}]");
    bool started = false;
    for (const auto& frame : r.link.sent) started |= contains(frame, "\"RemoteStartTransaction\"");
    TEST_ASSERT_TRUE(started);
}

void test_releasing_the_held_limit_stops_automatic_starts() {
    Rig r;
    r.wallbox.holdFixedLimit(10);
    r.wallbox.holdFixedLimit(std::nullopt);
    r.receive("[2,\"1\",\"StatusNotification\",{\"connectorId\":1,\"status\":\"Preparing\"}]");
    for (const auto& frame : r.link.sent) TEST_ASSERT_FALSE(contains(frame, "\"RemoteStartTransaction\""));
}

void test_draw_is_measured_when_reported_else_assumed_from_the_limit() {
    Rig r;
    r.status("Charging");
    r.charger->setAppliedAmps(10);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 10.0, r.wallbox.currentDrawAmps());
    TEST_ASSERT_FALSE(r.wallbox.drawIsMeasured());

    r.charger->setMeasuredAmps(8.5, r.platform.now);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 8.5, r.wallbox.currentDrawAmps());
    TEST_ASSERT_TRUE(r.wallbox.drawIsMeasured());
}

void test_request_status_asks_for_status_and_meter_values() {
    Rig r;
    r.wallbox.requestStatus(*r.charger);
    TEST_ASSERT_EQUAL(2, r.link.sent.size());
    TEST_ASSERT_TRUE(contains(r.link.sent[0], "\"StatusNotification\""));
    TEST_ASSERT_TRUE(contains(r.link.sent[1], "\"MeterValues\""));
}

// ---- registry and scheduler ----

void test_a_reconnecting_charger_replaces_its_stale_connection() {
    FakePlatform p;
    FakeLink a, b;
    ChargerRegistry registry;
    TEST_ASSERT_FALSE(registry.add(1, "box", a, p.now).has_value());
    auto replaced = registry.add(2, "box", b, p.now);
    TEST_ASSERT_TRUE(replaced.has_value());
    TEST_ASSERT_EQUAL(1, *replaced);
    TEST_ASSERT_EQUAL(1, registry.size());
    TEST_ASSERT_NULL(registry.find(1));
    TEST_ASSERT_NOT_NULL(registry.find(2));
}

void test_silent_chargers_are_found() {
    FakePlatform p;
    FakeLink a, b;
    ChargerRegistry registry;
    registry.add(1, "a", a, 0);
    registry.add(2, "b", b, 0);
    registry.find(2)->touch(290000);
    auto silent = registry.silentFor(310000, config::kChargerIdleTimeoutMillis);
    TEST_ASSERT_EQUAL(1, silent.size());
    TEST_ASSERT_EQUAL(1, silent[0]);
}

void test_scheduler_runs_tasks_when_due_and_only_once() {
    FakePlatform p;
    Scheduler s(p);
    int runs = 0;
    s.after(5000, [&] { runs++; });
    s.tick();
    TEST_ASSERT_EQUAL(0, runs);
    p.now += 5000;
    s.tick();
    s.tick();
    TEST_ASSERT_EQUAL(1, runs);
    TEST_ASSERT_EQUAL(0, s.pending());
}

void test_charger_id_comes_from_the_path_the_query_or_the_default() {
    check("mywallbox", ocpp::chargerIdFromUrl("/pair/mywallbox"));
    check("mywallbox/wattsyncpi", ocpp::chargerIdFromUrl("/pair/mywallbox/wattsyncpi/"));
    check(config::kDefaultChargerId, ocpp::chargerIdFromUrl("/pair"));
    check(config::kDefaultChargerId, ocpp::chargerIdFromUrl("/pair/"));
    check("abc", ocpp::chargerIdFromUrl("/pair?chargerId=abc"));
    check("mywallbox", ocpp::chargerIdFromUrl("/ocpp/wallbox/mywallbox"));
    check("mywallbox", ocpp::chargerIdFromUrl("/ocpp/wallbox/mywallbox/"));
    check("mywallbox", ocpp::chargerIdFromUrl("/ocpp/wallbox/mywallbox?x=1"));
    check("abc", ocpp::chargerIdFromUrl("/ocpp/wallbox?chargerId=abc"));
    check("abc", ocpp::chargerIdFromUrl("/ocpp/wallbox?x=1&chargerId=abc&y=2"));
    check("DEFAULT_BOX", ocpp::chargerIdFromUrl("/ocpp/wallbox"));
    check("DEFAULT_BOX", ocpp::chargerIdFromUrl("/"));
}

void test_status_names_round_trip() {
    TEST_ASSERT_EQUAL(ChargerStatus::SuspendedEVSE, parseChargerStatus("suspendedevse"));
    TEST_ASSERT_EQUAL(ChargerStatus::Unknown, parseChargerStatus("nonsense"));
    check("Charging", toString(ChargerStatus::Charging));
    TEST_ASSERT_TRUE(canStartCharging(ChargerStatus::Preparing));
    TEST_ASSERT_FALSE(canStartCharging(ChargerStatus::Charging));
    TEST_ASSERT_FALSE(canStartCharging(ChargerStatus::Available));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_call_result_uses_the_four_element_layout);
    RUN_TEST(test_call_error);
    RUN_TEST(test_set_charging_profile_matches_the_java_frame);
    RUN_TEST(test_remote_start_embeds_the_profile);
    RUN_TEST(test_remote_stop_and_trigger);
    RUN_TEST(test_uuid_has_the_standard_layout);
    RUN_TEST(test_boot_notification_is_accepted_with_the_heartbeat_interval);
    RUN_TEST(test_heartbeat_returns_the_time);
    RUN_TEST(test_unknown_action_gets_not_implemented);
    RUN_TEST(test_garbage_is_ignored);
    RUN_TEST(test_any_message_counts_as_a_sign_of_life);
    RUN_TEST(test_status_notification_updates_the_charger_and_ignores_connector_zero);
    RUN_TEST(test_start_transaction_assigns_increasing_ids_seeded_from_the_clock);
    RUN_TEST(test_stop_transaction_clears_the_transaction_and_limit);
    RUN_TEST(test_meter_values_current_is_recorded_and_the_transaction_learned);
    RUN_TEST(test_meter_values_power_is_converted_to_amps);
    RUN_TEST(test_a_measurement_goes_stale);
    RUN_TEST(test_a_refused_meter_request_is_remembered);
    RUN_TEST(test_a_rejected_meter_request_is_remembered);
    RUN_TEST(test_an_accepted_meter_request_keeps_asking);
    RUN_TEST(test_a_waiting_car_is_started_remotely_with_the_limit);
    RUN_TEST(test_remote_start_is_not_repeated_within_thirty_seconds);
    RUN_TEST(test_the_first_start_is_not_blocked_right_after_boot);
    RUN_TEST(test_a_charging_car_gets_a_profile_only_when_the_limit_changes);
    RUN_TEST(test_nothing_is_sent_without_a_known_transaction);
    RUN_TEST(test_zero_stops_a_charging_car_and_is_ignored_otherwise);
    RUN_TEST(test_a_closed_connection_gets_nothing);
    RUN_TEST(test_a_held_limit_starts_a_car_plugged_in_later);
    RUN_TEST(test_releasing_the_held_limit_stops_automatic_starts);
    RUN_TEST(test_draw_is_measured_when_reported_else_assumed_from_the_limit);
    RUN_TEST(test_request_status_asks_for_status_and_meter_values);
    RUN_TEST(test_a_reconnecting_charger_replaces_its_stale_connection);
    RUN_TEST(test_silent_chargers_are_found);
    RUN_TEST(test_scheduler_runs_tasks_when_due_and_only_once);
    RUN_TEST(test_charger_id_comes_from_the_path_the_query_or_the_default);
    RUN_TEST(test_status_names_round_trip);
    return UNITY_END();
}
