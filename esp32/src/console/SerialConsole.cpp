#include "console/SerialConsole.h"

#include <Arduino.h>
#include <WiFi.h>

#include <cstdlib>

#include "common/Config.h"
#include "common/Log.h"

namespace device {

using namespace solar;

void SerialConsole::poll() {
    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        if (c == '\r' || c == '\n') {
            if (!buffer_.empty()) run(buffer_);
            buffer_.clear();
        } else if (buffer_.size() < 64) {
            buffer_ += c;
        }
    }
}

void SerialConsole::printHelpHint() {
    Serial.println("Type help for the console commands (status, mode, amps, allowance, daylight, pause, resume, stop, ask, wifi, setup).");
}

void SerialConsole::printHelp() {
    Serial.println("Commands:");
    Serial.println("  status            mode, settings, meters, charger");
    Serial.println("  mode eco|maxsolar|fast   choose the charging mode (fast = the Custom current)");
    Serial.println("  amps N            Custom current 6 to 40 A, and switch to fast mode");
    Serial.println("  allowance W       grid allowance 0 to 1000 W (Eco and Max solar)");
    Serial.println("  daylight on|off   pause Eco and Max solar outside the daylight window, or not");
    Serial.println("  rate C            electricity price in cents per kWh (0 to 200), for the savings");
    Serial.println("  pause             kill switch: stop charging and keep it stopped, whatever the mode");
    Serial.println("  resume            end the pause: back to the mode set before");
    Serial.println("  stop              stop the charge now (Eco and Max solar may restart it by their rules)");
    Serial.println("  ask               ask the charger for its status and meter values");
    Serial.println("  wifi              which network the board is on (never the password)");
    Serial.println("  setup             open the setup network (Brimwatt-Setup) to change Wi-Fi, the Enphase token, the Envoy address");
    Serial.println("  setup clear       forget everything saved from the setup page (secrets.h applies again) and restart");
    Serial.println("  tesla mode on|off            Tesla mode (off by default): Bluetooth, the brimwatt-tesla Wi-Fi and the car in command; restarts the board");
    Serial.println("  tesla scan [seconds]         list every Tesla in range with its signal (no VIN needed; 10-300 s, default 60)");
    Serial.println("  tesla scan <VIN> [seconds]   listen for your own car only; nothing is sent to the car");
    Serial.println("  tesla pair <VIN>    add this controller's own key to your car (you tap your key card in the car); VIN is never printed");
    Serial.println("  tesla status        read-only: is the car awake, battery, charge limit, charging current (changes nothing, does not wake it)");
    Serial.println("  tesla charge start|stop       start or stop the car's charge (CHANGES the car; never wakes it)");
    Serial.println("  tesla charge amps <6-40>      set the car's charging current (CHANGES the car; never wakes it)");
    Serial.println("  tesla debug on|off            print every message to and from the car as hex (for trying a new car model)");
    Serial.println("  tesla wake                    wake a sleeping car (it stays awake a while and uses a little battery)");
    Serial.println("  tesla key           show this controller's key fingerprint and whether a car is paired");
    Serial.println("  tesla forget        delete the key and car from this board (then remove the key in the car: Controls > Locks)");
    Serial.println("  tesla selftest      check this board's cryptography against Tesla's published example values");
    Serial.println("  tesla stop        end a scan, pairing, status or charge command early");
    Serial.println("  help              this list");
}

std::string SerialConsole::statusLine() {
    const SystemState& state = controller_.state();
    char meters[96] = "meters: no reading yet";
    if (auto reading = meters_.lastReading()) {
        snprintf(meters, sizeof meters, "solar %.0f W, grid %.0f W (%s), %lu s ago", reading->solarWatts,
                 reading->gridWatts, reading->gridWatts < 0 ? "exporting" : "importing",
                 (unsigned long)((millis() - meters_.lastSuccessMillis()) / 1000));
    }
    char envoy[120] = "";
    if (!state.enphaseOk()) snprintf(envoy, sizeof envoy, " | ENVOY ERROR: %s", state.enphaseMessage().c_str());

    char line[480];
    snprintf(line, sizeof line,
             "mode %s%s%s | %s%s | charger: %s | allowance %d W, daylight %s (%s) | up %lu min, heap %u KB (block %u KB), Wi-Fi %d dBm | "
             "envoy ok %lu fail %lu",
             toString(state.mode()), state.paused() ? " PAUSED" : "", state.failSafe() ? " (FAIL-SAFE)" : "", meters, envoy, wallbox_.describe().c_str(),
             state.gridAllowanceWatts(), state.daylightWindowEnabled() ? "on" : "off", controller_.daylightText().c_str(),
             (unsigned long)(millis() / 60000), (unsigned)(ESP.getFreeHeap() / 1024),
             (unsigned)(ESP.getMaxAllocHeap() / 1024), WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0, (unsigned long)meters_.okCount(),
             (unsigned long)meters_.failCount());
    return line;
}

void SerialConsole::run(const std::string& line) {
    if (line == "help" || line == "?") {
        printHelp();
    } else if (line == "status") {
        LOG_INFO("%s", statusLine().c_str());
        LOG_INFO("custom current %d A | last command sent: %s", controller_.state().fastModeAmps(),
                 wallbox_.lastCommand().c_str());
    } else if (line == "pause" || line == "resume") {
        controller_.setPaused(line == "pause");
    } else if (line == "stop") {
        controller_.stopNow();
    } else if (line == "wifi") {
        if (WiFi.status() == WL_CONNECTED) {
            LOG_INFO("Wi-Fi: \"%s\", %d dBm, IP %s", WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str());
        } else {
            LOG_WARN("Wi-Fi: not connected.");
        }
    } else if (line == "setup") {
        if (!portal_) {
            LOG_WARN("Console: the setup network is not available.");
        } else if (portal_->isActive()) {
            LOG_INFO("Setup: already open. Join \"%s\" and open http://%s/setup", solar::setup::kApName, solar::setup::kPortalAddress);
        } else {
            portal_->open(SetupReason::Manual);
        }
    } else if (line == "setup clear") {
        if (!credentialStore_) {
            LOG_WARN("Console: the setup network is not available.");
        } else {
            credentialStore_->clear();
            LOG_WARN("Setup: everything saved from the setup page is forgotten. Restarting with the values in secrets.h.");
            delay(300);
            ESP.restart();
        }
    } else if (line == "tesla mode on" || line == "tesla mode off") {
        const bool on = line == "tesla mode on";
        if (!teslaMode_) LOG_WARN("Console: Tesla mode is not available.");
        else if (teslaMode_->enabled() == on) LOG_INFO("Tesla mode is already %s.", on ? "on" : "off");
        else teslaMode_->setEnabled(on);
    } else if (line == "tesla stop") {
        if (teslaCar_ && teslaCar_->busy()) teslaCar_->cancel();
        else if (teslaScan_ && teslaScan_->running()) teslaScan_->cancel();
        else LOG_INFO("Tesla: nothing is running.");
    } else if (line == "tesla pair" || line.rfind("tesla pair ", 0) == 0) {
        // Never echo this line back: it holds the VIN.
        if (!teslaCar_) {
            LOG_WARN("Console: Tesla control is not available.");
        } else if (teslaScan_ && teslaScan_->running()) {
            LOG_WARN("Tesla: a scan is running. Type \"tesla stop\" first.");
        } else {
            std::string vin = line.size() > 11 ? line.substr(11) : "";
            std::string err = teslaCar_->startPair(vin);
            if (!err.empty()) LOG_WARN("Tesla pair: %s", err.c_str());
        }
    } else if (line == "tesla status") {
        if (!teslaCar_) {
            LOG_WARN("Console: Tesla control is not available.");
        } else if (teslaScan_ && teslaScan_->running()) {
            LOG_WARN("Tesla: a scan is running. Type \"tesla stop\" first.");
        } else {
            std::string err = teslaCar_->startStatus();
            if (!err.empty()) LOG_WARN("Tesla status: %s", err.c_str());
        }
    } else if (line == "tesla charge start" || line == "tesla charge stop" || line == "tesla charge amps" || line.rfind("tesla charge amps ", 0) == 0) {
        if (!teslaCar_) {
            LOG_WARN("Console: Tesla control is not available.");
        } else if (teslaScan_ && teslaScan_->running()) {
            LOG_WARN("Tesla: a scan is running. Type \"tesla stop\" first.");
        } else {
            using Cmd = solar::tesla::ChargeCommandFlow;
            Cmd::Command command = line == "tesla charge start" ? Cmd::Start : line == "tesla charge stop" ? Cmd::Stop : Cmd::SetAmps;
            int amps = 0;
            bool valid = true;
            if (command == Cmd::SetAmps) {
                std::string n = line.size() > 18 ? line.substr(18) : "";
                valid = !n.empty() && n.size() <= 3 && n.find_first_not_of("0123456789") == std::string::npos;
                if (valid) amps = std::atoi(n.c_str());
            }
            std::string err = valid ? teslaCar_->startCharge(command, amps) : "Give the current in whole amps, 6 to 40: tesla charge amps 16";
            if (!err.empty()) LOG_WARN("Tesla charge: %s", err.c_str());
        }
    } else if (line == "tesla wake") {
        if (!teslaCar_) {
            LOG_WARN("Console: Tesla control is not available.");
        } else if (teslaScan_ && teslaScan_->running()) {
            LOG_WARN("Tesla: a scan is running. Type \"tesla stop\" first.");
        } else {
            std::string err = teslaCar_->startWake();
            if (!err.empty()) LOG_WARN("Tesla wake: %s", err.c_str());
        }
    } else if (line == "tesla debug on" || line == "tesla debug off") {
        if (!teslaCar_) {
            LOG_WARN("Console: Tesla control is not available.");
        } else {
            teslaCar_->setDebug(line == "tesla debug on");
            LOG_INFO("Tesla: message dump %s.", teslaCar_->debug() ? "on" : "off");
        }
    } else if (line == "tesla key") {
        if (!teslaCar_) {
            LOG_WARN("Console: Tesla control is not available.");
        } else if (!teslaCar_->hasKey()) {
            LOG_INFO("Tesla: no key yet. It is made the first time you run tesla pair <VIN>.");
        } else {
            LOG_INFO("Tesla: key fingerprint %s. %s", teslaCar_->keyFingerprint().c_str(), teslaCar_->paired() ? "A car is paired." : "No car is paired yet.");
        }
    } else if (line == "tesla forget") {
        if (teslaCar_) teslaCar_->forget(); else LOG_WARN("Console: Tesla control is not available.");
    } else if (line == "tesla selftest") {
        if (!teslaCar_) {
            LOG_WARN("Console: Tesla control is not available.");
        } else {
            std::string report;
            uint32_t t = millis();
            bool ok = teslaCar_->selfTest(report);
            if (ok) LOG_INFO("Tesla selftest: all of Tesla's published example values check out on this board (%lu ms).", static_cast<unsigned long>(millis() - t));
            else LOG_WARN("Tesla selftest: FAILED: %s", report.c_str());
        }
    } else if (line == "tesla scan" || (line.rfind("tesla scan ", 0) == 0 && line.size() <= 14 && line.find_first_not_of("0123456789", 11) == std::string::npos)) {
        if (!teslaScan_) {
            LOG_WARN("Console: the Tesla scan is not available.");
        } else {
            uint32_t seconds = line.size() > 11 ? static_cast<uint32_t>(atoi(line.c_str() + 11)) : 60;
            std::string err = teslaCar_ && teslaCar_->busy() ? "A Tesla pairing or status job is running. Type \"tesla stop\" first." : teslaScan_->startDiscover(seconds);
            if (!err.empty()) LOG_WARN("Tesla scan: %s", err.c_str());
        }
    } else if (line.rfind("tesla scan ", 0) == 0) {
        if (!teslaScan_) {
            LOG_WARN("Console: the Tesla scan is not available.");
        } else {
            std::string rest = line.substr(11);
            size_t sp = rest.find(' ');
            std::string vin = rest.substr(0, sp);
            uint32_t seconds = 60;
            if (sp != std::string::npos) seconds = static_cast<uint32_t>(atoi(rest.c_str() + sp + 1));
            std::string err = teslaCar_ && teslaCar_->busy() ? "A Tesla pairing or status job is running. Type \"tesla stop\" first." : teslaScan_->start(vin, seconds);
            if (!err.empty()) LOG_WARN("Tesla scan: %s", err.c_str());
        }
    } else if (line == "ask") {
        if (!wallbox_.hasConnectedChargers()) {
            LOG_WARN("Console: no charger is connected.");
        } else {
            wallbox_.requestStatusAll();
        }
    } else if (line.rfind("mode ", 0) == 0) {
        auto mode = parseChargingMode(line.substr(5));
        if (!mode) {
            LOG_WARN("Console: mode must be eco, maxsolar or fast.");
        } else {
            controller_.setMode(*mode);
        }
    } else if (line.rfind("amps ", 0) == 0) {
        int amps = std::atoi(line.c_str() + 5);
        if (!controller_.setFastAmps(amps)) {
            LOG_WARN("Console: amps must be %d to %d.", config::kMinAmps, config::kMaxFastAmps);
        } else {
            controller_.setMode(ChargingMode::Fast);
        }
    } else if (line.rfind("allowance ", 0) == 0) {
        int watts = std::atoi(line.c_str() + 10);
        if (!controller_.setGridAllowance(watts)) {
            LOG_WARN("Console: allowance must be 0 to %d W.", config::kMaxGridAllowanceWatts);
        }
    } else if (line.rfind("rate ", 0) == 0) {
        int cents = std::atoi(line.c_str() + 5);
        if (!controller_.setRateCents(cents)) {
            LOG_WARN("Console: rate must be 0 to %d cents per kWh.", solar::config::kMaxRateCents);
        }
    } else if (line == "daylight on" || line == "daylight off") {
        controller_.setDaylightWindowEnabled(line == "daylight on");
    } else {
        Serial.printf("Unknown command \"%s\". Type help.\n", line.c_str());
    }
}

}  // namespace device
