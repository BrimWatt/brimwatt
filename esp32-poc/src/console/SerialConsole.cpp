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
    Serial.println("Type help for the console commands (status, mode, amps, allowance, daylight, pause, resume, stop, ask).");
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
