// ESP32-S3 solar charger firmware: reads the Enphase Envoy, plans the charging current (ECO, MAX_SOLAR, Custom)
// and runs the OCPP 1.6J server the wallbox connects to.
//
// This file only wires the modules together; each piece lives in its own folder:
//   lib/solar_core/   portable logic, tested on the computer: OCPP, wallbox controller, planner, daylight window,
//                     the solar controller that ties them together, the dashboard API
//   src/platform/     clock, random numbers and the flash settings store for the core, serial logging
//   src/net/          Wi-Fi (with a recognisable device name), the setup network, network time, mDNS, OTA
//   src/enphase/      Envoy client and the background meter poller
//   src/ocpp_server/  the WebSocket endpoint the wallbox connects to
//   src/web/          the dashboard: embedded page and HTTP server in front of the portable API (lib/solar_core/web)
//   src/console/      commands typed in the serial monitor

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>

#include "ble/CarService.h"
#include "car/CarCommander.h"
#include "car/CarLinkSwitch.h"
#include "car/ChargeRouter.h"
#include "ble/TeslaCar.h"
#include "ble/TeslaScan.h"
#include "platform/NvsTeslaStore.h"
#include "common/Config.h"
#include "common/Log.h"
#include "common/Scheduler.h"
#include "config/DeviceConfig.h"
#include "console/SerialConsole.h"
#include "control/SolarController.h"
#include "daylight/DaylightWindow.h"
#include "enphase/EnphaseClient.h"
#include "enphase/MeterPoller.h"
#include "fleet/FleetApi.h"
#include "fleet/FleetAuth.h"
#include "fleet/FleetCar.h"
#include "fleet/FleetControl.h"
#include "net/EspFleetExecutor.h"
#include "net/EspFleetHttp.h"
#include "platform/NvsFleetStore.h"
#include "platform/TeslaIdentity.h"
#include "net/Mdns.h"
#include "net/EspNotifySender.h"
#include "net/Ota.h"
#include "net/SetupPortal.h"
#include "net/TeslaMode.h"
#include "notify/Notifier.h"
#include "net/TimeSync.h"
#include "platform/SiteSettings.h"
#include "site/Site.h"
#include "net/WifiConnection.h"
#include "ocpp/CoreHandlers.h"
#include "ocpp/OcppRouter.h"
#include "ocpp_server/OcppServer.h"
#include "platform/ArduinoPlatform.h"
#include "platform/EspDiagnostics.h"
#include "history/HistoryService.h"
#include "platform/FileHistoryStore.h"
#include "platform/NvsChargerStateStore.h"
#include "platform/NvsCredentialStore.h"
#include "platform/NvsSettingsStore.h"
#include "state/ChargerPersistence.h"
#include "secrets.h"
#include "setup/SetupApi.h"
#include "setup/SetupLogic.h"
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
static device::TeslaScan teslaScan;
static device::NvsTeslaStore teslaStore;
static device::TeslaCar teslaCar(teslaStore);
static device::CarService carService(teslaCar, teslaStore, teslaScan);
// The Tesla account link (Fleet API): the same car control over the internet. Which of the two links is used is chosen at start-up
// (carLink.select, from the saved "Connect through" choice); the other one is never started.
static device::NvsFleetStore fleetStore;
static device::EspFleetHttp fleetHttp;
static device::EspFleetExecutor fleetExecutor;
static device::TeslaIdentity teslaIdentity(teslaStore);
static device::MbedTeslaCrypto fleetCrypto;
static solar::fleet::FleetAuth fleetAuth(fleetHttp, fleetStore, platform);
static solar::fleet::FleetApi fleetApi(fleetHttp, fleetAuth);
static solar::fleet::FleetCar fleetCar(fleetApi, fleetAuth, teslaIdentity, fleetCrypto, platform, fleetExecutor);
static solar::fleet::FleetManager fleetManager(fleetAuth, teslaIdentity, fleetCrypto);
static solar::CarLinkSwitch carLink(carService, fleetCar);
// Who is in command of the charge: the OCPP wallbox (default) or, if the owner chose it, the car (over Bluetooth or the Tesla account).
static solar::CarCommander carCommander(platform, carLink);
static solar::ChargeRouter chargeRouter(wallbox, carCommander, carLink);
static SolarController controller(platform, state, chargeRouter, daylight, settingsStore);

static device::EspDiagnostics diagnostics(__DATE__ " " __TIME__);
static device::WifiConnection wifi(device::kHostname);
static device::TimeSync timeSync;
static device::SiteSettings siteSettings;   // where the panels are; read first thing in setup()
static std::string siteClockRule = device::kTimezone;   // the clock rule for that site
static device::EnphaseClient envoy;   // told its token and address in setup(), once they are known
static device::MeterPoller meterPoller(envoy);
static device::SerialConsole console(controller, wallbox, meterPoller);
static device::EspNotifySender notifySender;
static Notifier notifier(platform, state, notifySender);
static ApiHandler api(platform, controller, chargeRouter, daylight);

// The setup network and its page. The values in secrets.h are the defaults; what was saved from the setup page wins.
static solar::setup::Credentials compiledDefaults() {
    return {WIFI_SSID, WIFI_PASSWORD, ENPHASE_TOKEN, ENVOY_IP, OTA_PASSWORD};
}
static device::NvsCredentialStore credentialStore;
static device::SetupPortal portal;
static device::TeslaMode teslaMode(teslaStore, portal);
static solar::setup::SetupApi setupApi(credentialStore, portal, compiledDefaults());
static device::WebServerAdapter webServer(api, setupApi, portal);
static solar::setup::Credentials credentials;   // the values in use

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

/** Keeps everything that must keep working while the board has no Wi-Fi: the setup page, its network, the console. */
static void serviceWhileOffline() {
    webServer.loop();
    portal.loop();
    teslaMode.loop();
    siteSettings.loop();
    console.poll();
    teslaScan.loop();   // Tesla jobs must also work with no Wi-Fi (the car is reached over Bluetooth)
    carService.loop();
    delay(5);
}

/**
 * Joins the Wi-Fi with the saved (or compiled-in) details and returns once connected. With nothing to join, or when joining
 * keeps failing, the setup network opens so the details can be fixed from a phone. A Wi-Fi change saved from the setup page
 * is only a trial: if the board cannot join the new network, the old one is put back and the page says why.
 */
static void bringUpNetwork() {
    using namespace solar::setup;
    credentials = effective(credentialStore.load(), compiledDefaults());
    bool trial = credentialStore.loadTrial().pending;
    bool trialFailed = false;
    bool offeredSetup = false;

    if (!credentials.hasWifi() && teslaMode.enabled()) {
        // Tesla mode works without the home Wi-Fi (its own hotspot is already open); hold BOOT or type "setup" to add one.
        LOG_WARN("Wi-Fi: no network is set. Tesla mode carries on with its own hotspot.");
        return;
    }
    if (!credentials.hasWifi()) {
        LOG_WARN("Wi-Fi: no network is set.");
        portal.open(device::SetupReason::NoWifi);
        offeredSetup = true;
    } else if (trial) {
        LOG_INFO("Wi-Fi: trying the network just chosen on the setup page: \"%s\"", credentials.wifiSsid.c_str());
    }

    const uint32_t started = millis();
    uint32_t nextAttempt = millis();
    while (!wifi.isConnected()) {
        serviceWhileOffline();
        if (teslaMode.enabled() && !trial && millis() - started > device::kTeslaJoinMillis) {
            // Away from home the home Wi-Fi is not there. Keep looking for it now and then (loop()), not all the time: a board
            // that keeps scanning for a missing network keeps interrupting its own hotspot.
            LOG_WARN("Wi-Fi: not joined after %lu s. Tesla mode carries on with its own hotspot.", (unsigned long)(device::kTeslaJoinMillis / 1000));
            WiFi.setAutoReconnect(false);
            WiFi.disconnect(false);
            return;
        }
        if (!credentials.hasWifi() || static_cast<int32_t>(millis() - nextAttempt) < 0) continue;
        if (portal.clientsConnected()) {   // somebody is using the setup page: a join attempt would interrupt them
            nextAttempt = millis() + 5000;
            continue;
        }
        if (teslaScan.running() || teslaCar.busy()) {   // a Tesla job is using the radio and needs the loop: no blocking join now
            nextAttempt = millis() + 5000;
            continue;
        }
        if (wifi.connect(credentials.wifiSsid, credentials.wifiPassword, device::kWifiConnectTimeoutMillis, portal.isActive() || teslaMode.enabled())) break;

        nextAttempt = millis() + (portal.isActive() ? device::kSetupRetryMillis : 5000);
        if (trial) {
            // The new network does not work: put the old one back, remember why, and show the setup page again.
            trial = false;
            trialFailed = true;
            finishTrial(credentialStore, false, wifi.lastFailure());
            credentials = effective(credentialStore.load(), compiledDefaults());
            LOG_WARN("Wi-Fi: %s", credentialStore.loadTrial().lastError.c_str());
            if (!portal.isActive()) portal.open(device::SetupReason::JoinFailed);
            offeredSetup = true;
            nextAttempt = millis();   // try the old network straight away
        } else if (!offeredSetup && millis() - started > device::kSetupAfterFailMillis) {
            offeredSetup = true;   // offered once per start; hold the BOOT button to open it again later
            portal.open(device::SetupReason::JoinFailed);
        }
    }

    if (trial) finishTrial(credentialStore, true, "");   // the new network works: keep it
    // After a failed trial the setup network stays open for its time window so the reason can be read on the phone.
    if (portal.isActive() && !trialFailed) portal.close();
    // An old error is shown once; a normal start clears it.
    if (!trialFailed && !credentialStore.loadTrial().lastError.empty()) credentialStore.saveTrial(solar::setup::TrialState{});
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
    LOG_INFO("Point the wallbox at ws://%s.local:%u/pair  (or ws://%s:%u/pair); it adds its own id",
             device::kHostname, (unsigned)config::kOcppPort, WiFi.localIP().toString().c_str(), (unsigned)config::kOcppPort);
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

    // The web server starts before the Wi-Fi is up, so the setup page can be served when there is no Wi-Fi to join.
    WiFi.setHostname(device::kHostname);
    WiFi.mode(WIFI_STA);
    portal.setBeforeRestart([] {
        if (history.dirty() || history.longPending()) historyStore.save(history);   // nothing recent is lost across the restart
    });
    teslaMode.setBeforeRestart([] {
        if (history.dirty() || history.longPending()) historyStore.save(history);
    });
    siteSettings.setBeforeRestart([] {
        if (history.dirty() || history.longPending()) historyStore.save(history);
    });
    console.setSetup(&portal, &credentialStore);
    console.setTesla(&teslaScan, &teslaCar);
    console.setTeslaMode(&teslaMode);
    siteSettings.begin();   // where the panels are: the daylight window and the clock start from it (restart after a change)
    daylight.setLocation(siteSettings.current().latitude, siteSettings.current().longitude);
    if (auto rule = solar::timezones::posixFor(siteSettings.current().timezone)) siteClockRule = *rule;
    teslaMode.begin();
    carLink.select(teslaMode.apiLink());   // Connect through: Bluetooth (default) or the Tesla account
    fleetAuth.reload();                    // the flash could not be read yet when the objects were built
    // Tesla mode: off unless the owner turned it on: Bluetooth stays off, and the board's own Wi-Fi opens only when it is on
    webServer.begin();
    bringUpNetwork();
    envoy.configure(credentials.enphaseToken, credentials.envoyIp);
    api.setMeterLinked(credentials.hasMeter());   // "paired" for the dashboard's notices

    timeSync.begin(WiFi.gatewayIP().toString().c_str(), siteClockRule.c_str());   // in the background: the dashboard must not wait for the internet
    device::Mdns::start(device::kHostname, config::kOcppPort);
    if (credentials.otaPassword.empty()) {
        LOG_WARN("OTA: no update password is set, so updating over Wi-Fi is off. Set one on the setup page.");
    } else {
        device::Ota::begin(device::kHostname, credentials.otaPassword.c_str(), [] {
            if (history.dirty() || history.longPending()) historyStore.save(history);   // nothing recent is lost across the restart
        });
    }

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
    api.setCar(&carLink);
    api.setFleet(&fleetManager);
    api.setRouter(&chargeRouter);
    api.setTeslaMode(&teslaMode);
    api.setSite(&siteSettings);
    ocppServer.setHistory(&history);
    controller.restoreSettings();
    chargeRouter.setSettings(&teslaStore);
    chargeRouter.onChanged([] { controller.onCommanderChanged(); });
    chargeRouter.restore();   // the car only takes command again if it was chosen and is still paired
    chargerPersistence.restore();

    console.printHelpHint();
    if (device::TimeSync::isValid()) startControl();   // otherwise loop() starts it once the clock is set
}

/** Tesla mode decides who is in command (the car when on, the charger when off); once a second is plenty. */
static void followTeslaMode() {
    static uint32_t last = 0;
    if (millis() - last < 1000) return;
    last = millis();
    chargeRouter.enforceMode(teslaMode.enabled());
    if (!teslaMode.enabled() || wifi.isConnected() || !credentials.hasWifi()) return;

    // Tesla mode without the home Wi-Fi (e.g. next to a friend's car): look for it again now and then, but only when nobody is using the
    // hotspot and no Bluetooth job is running, because a join attempt blocks the board for a while and hops the radio's channel.
    static uint32_t lastTry = 0;
    if (millis() - lastTry < device::kTeslaRejoinMillis || WiFi.softAPgetStationNum() > 0) return;
    if (teslaCar.busy() || teslaScan.running() || portal.isActive()) return;
    lastTry = millis();
    if (!wifi.connect(credentials.wifiSsid, credentials.wifiPassword, 15000, true)) {
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false);   // stop searching in the background until the next try
    }
}

void loop() {
    device::Ota::loop();
    webServer.loop();
    portal.loop();   // the BOOT button, and the setup network when it is open
    teslaMode.loop();
    siteSettings.loop();
    console.poll();
    teslaScan.loop();
    carService.loop();
    fleetCar.loop();
    carCommander.loop();
    followTeslaMode();
    // Home Wi-Fi lost while running: rejoin by ourselves. Tesla mode has its own slower retry (followTeslaMode), and the setup network
    // owns the radio while it is open.
    if (!teslaMode.enabled() && !portal.isActive()) wifi.keepAlive();

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
    static uint32_t lastMemoryWatch = 0;
    if (millis() - lastMemoryWatch >= 5000) {   // memory samples and the lowest point, for Diagnostics
        lastMemoryWatch = millis();
        diagnostics.service(platform.epochSeconds());
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
