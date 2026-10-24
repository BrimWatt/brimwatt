// ESP32-S3 solar charger firmware: reads the Enphase Envoy, plans the charging current (ECO, MAX_SOLAR, Custom)
// and runs the OCPP 1.6J server the wallbox connects to.
//
// This file only wires the modules together; each piece lives in its own folder:
//   lib/solar_core/   portable logic, tested on the computer: OCPP, wallbox controller, planner, daylight window,
//                     the solar controller that ties them together, the dashboard API
//   src/platform/     clock, random numbers and the flash settings store for the core, serial logging
//   src/net/          Wi-Fi (with a recognisable device name), network time, mDNS
//   src/enphase/      Envoy client and the background meter poller
//   src/ocpp_server/  the WebSocket endpoint the wallbox connects to
//   src/web/          the dashboard: embedded page and HTTP server in front of the portable API (lib/solar_core/web)
//   src/console/      commands typed in the serial monitor

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>

#include "common/Config.h"
#include "common/Log.h"
#include "common/Scheduler.h"
#include "config/DeviceConfig.h"
#include "console/SerialConsole.h"
#include "control/SolarController.h"
#include "daylight/DaylightWindow.h"
#include "enphase/EnphaseClient.h"
#include "enphase/MeterPoller.h"
#include "net/Mdns.h"
#include "net/EspNotifySender.h"
#include "net/Ota.h"
#include "notify/Notifier.h"
#include "net/TimeSync.h"
#include "net/WifiConnection.h"
#include "ocpp/CoreHandlers.h"
#include "ocpp/OcppRouter.h"
#include "ocpp_server/OcppServer.h"
#include "platform/ArduinoPlatform.h"
#include "platform/EspDiagnostics.h"
#include "history/HistoryService.h"
#include "platform/FileHistoryStore.h"
#include "platform/NvsChargerStateStore.h"
#include "platform/NvsSettingsStore.h"
#include "state/ChargerPersistence.h"
#include "secrets.h"
#include "state/RateSchedule.h"
#include "state/SystemState.h"
#include "wallbox/ChargerRegistry.h"
#include "wallbox/WallboxController.h"
#include "web/ApiHandler.h"
#include "web/WebServerAdapter.h"

using namespace solar;

static device::ArduinoPlatform platform;
static Scheduler scheduler(platform);
static ChargerRegistry chargers;
static ocpp::OcppRouter router(platform);
static WallboxController wallbox(platform, chargers);
static device::NvsChargerStateStore chargerStore;
static ChargerPersistence chargerPersistence(platform, chargers, chargerStore);
static device::OcppServer ocppServer(platform, chargers, router, wallbox, scheduler, chargerPersistence);

static SystemState state;
static HistoryService history(platform);
static device::FileHistoryStore historyStore;
static DaylightWindow daylight(platform, config::kSiteLatitude, config::kSiteLongitude,
                               config::kDaylightStartAfterSunriseSeconds, config::kDaylightEndBeforeSunsetSeconds);
static device::NvsSettingsStore settingsStore;
static SolarController controller(platform, state, wallbox, daylight, settingsStore);

static device::EspDiagnostics diagnostics(__DATE__ " " __TIME__);
static device::WifiConnection wifi(device::kHostname);
static device::TimeSync timeSync;
static device::EnphaseClient envoy(ENPHASE_TOKEN, ENVOY_IP);
static device::MeterPoller meterPoller(envoy);
static device::SerialConsole console(controller, wallbox, meterPoller);
static device::EspNotifySender notifySender;
static Notifier notifier(platform, state, notifySender);
static ApiHandler api(platform, controller, wallbox, daylight);
static device::WebServerAdapter webServer(api);

static uint32_t lastCycle = 0;
static uint32_t lastStatusLine = 0;
static uint32_t lastHistorySave = 0;
static uint32_t lastJournalSave = 0;

static void printBoardInfo() {
    Serial.println();
    Serial.println("=== ESP32-S3 solar charger ===");
    Serial.printf("Chip:   %s rev %d, %d core(s), %d MHz\n", ESP.getChipModel(), ESP.getChipRevision(),
                  ESP.getChipCores(), ESP.getCpuFreqMHz());
    Serial.printf("Flash:  %u MB   PSRAM: %u MB   Heap: %u KB free\n", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)),
                  (unsigned)(ESP.getPsramSize() / (1024 * 1024)), (unsigned)(ESP.getFreeHeap() / 1024));
    Serial.printf("Name:   %s\n", device::kHostname);
    bool suspicious = false;
    const esp_reset_reason_t reason = esp_reset_reason();
    const char* text = device::resetReasonText(reason, suspicious);
    Serial.printf("%sLast restart: %s (code %d)\n", suspicious ? "!!! " : "", text, (int)reason);
}

static void waitForWifi() {
    while (!wifi.connect(device::kWifiConnectTimeoutMillis)) delay(5000);
}

static bool controlStarted = false;

/**
 * Everything that needs the clock: the OCPP server (replies carry the time and the wallbox sets its clock from them,
 * so it must never be served a 1970 date), the meter polling, and the restart counter with its crash time. It starts
 * as soon as the clock is set, which can be later than the dashboard and the Wi-Fi updates if the internet is down.
 */
static void startControl() {
    controlStarted = true;
    diagnostics.recordBoot(platform.epochSeconds());
    LOG_INFO("Clock is set: starting the OCPP server and the meter polling.");
    LOG_INFO("[DAYLIGHT] Today's window: %s", controller.daylightText().c_str());
    ocpp::registerCoreHandlers(router, platform, [](Charger& charger) { wallbox.onChargerChanged(charger); }, &history);
    ocppServer.begin();
    LOG_INFO("Point the wallbox at ws://%s:%u/ocpp/wallbox/<chargerId>  (or ws://%s.local:%u/...)",
             WiFi.localIP().toString().c_str(), (unsigned)config::kOcppPort, device::kHostname,
             (unsigned)config::kOcppPort);
    meterPoller.start();
    // First control cycle a few seconds from now, then every poll interval.
    lastCycle = millis() - device::kPollIntervalMillis + 5000;
    lastStatusLine = millis();
}

void setup() {
    Serial.begin(115200);
    // On the native USB port Serial only counts as open once a monitor is attached, so wait for it (up to
    // 10 s) instead of printing the boot messages into the void.
    for (uint32_t waited = millis(); !Serial && millis() - waited < 10000;) delay(50);
    delay(500);
    device::installSerialLogSink();
    printBoardInfo();

    waitForWifi();
    timeSync.begin(WiFi.gatewayIP().toString().c_str());   // in the background: the dashboard must not wait for the internet
    device::Mdns::start(device::kHostname, config::kOcppPort);
    device::Ota::begin(device::kHostname, OTA_PASSWORD, [] {
        if (history.dirty() || history.longPending()) historyStore.save(history);   // nothing recent is lost across the restart
    });

    // The year of history lives in PSRAM, not in the small internal memory the network code needs.
    solar::bigAllocator() = [](size_t bytes) -> void* {
        void* block = ps_malloc(bytes);
        return block ? block : malloc(bytes);
    };
    if (historyStore.begin()) historyStore.load(history);
    controller.setHistory(&history);
    history.setPriceProvider([](int64_t t) {   // the price at that hour of the local day
        const int hour = static_cast<int>(((t + platform.utcOffsetSeconds(t)) % 86400 + 86400) % 86400 / 3600);
        return static_cast<double>(RateSchedule::centsAt(state.rateCents(), state.peakRateCents(), state.peakStartHour(),
                                                         state.peakEndHour(), hour));
    });
    history.setEventListener([](const std::string& text) { notifier.onEvent(text); });
    api.setNotifier(&notifier);
    api.setHistory(&history);
    api.setDiagnostics(&diagnostics);
    ocppServer.setHistory(&history);
    controller.restoreSettings();
    chargerPersistence.restore();

    webServer.begin();
    console.printHelpHint();
    if (device::TimeSync::isValid()) startControl();   // otherwise loop() starts it once the clock is set
}

void loop() {
    device::Ota::loop();
    webServer.loop();
    console.poll();

    if (!controlStarted) {
        if (device::TimeSync::isValid()) {
            startControl();
        } else {
            static uint32_t lastWaitingLine = 0;
            if (millis() - lastWaitingLine > 15000) {
                lastWaitingLine = millis();
                LOG_WARN("Waiting for the clock (internet time or the router). The dashboard and Wi-Fi updates already work.");
            }
            delay(2);
            return;
        }
    }
    ocppServer.loop();
    scheduler.tick();
    chargerPersistence.tick();

    // A finished meter reading goes to the controller, which decides what the wallbox should do.
    if (auto result = meterPoller.takeResult()) controller.onMeterResult(*result);

    if (!wifi.isConnected()) {
        // Auto-reconnect is on; just say so once in a while instead of hammering the radio.
        static uint32_t lastWarning = 0;
        if (millis() - lastWarning > 30000) {
            lastWarning = millis();
            LOG_WARN("Wi-Fi: connection lost, waiting for it to come back.");
        }
    }

    if (millis() - lastCycle >= device::kPollIntervalMillis) {
        lastCycle = millis();
        if (controller.tick()) meterPoller.request();
    }
    if (millis() - lastJournalSave >= 60UL * 1000) {   // every minute: a small journal of what is new
        lastJournalSave = millis();
        historyStore.saveJournal(history);
    }
    if (millis() - lastHistorySave >= 30UL * 60 * 1000) {   // every half hour: the full save, only if something changed
        lastHistorySave = millis();
        if (history.dirty() || history.longPending()) historyStore.save(history);
    }
    if (millis() - lastStatusLine >= device::kPollIntervalMillis) {
        lastStatusLine = millis();
        LOG_INFO("%s", console.statusLine().c_str());
        LOG_INFO("Memory: %u KB heap free, largest block %u KB, %u KB PSRAM free, up %lu min", (unsigned)(ESP.getFreeHeap() / 1024),
                 (unsigned)(ESP.getMaxAllocHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024), (unsigned long)(millis() / 60000));
    }
    delay(2);
}
