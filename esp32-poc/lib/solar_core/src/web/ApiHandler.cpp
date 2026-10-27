#include "web/ApiHandler.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <optional>

#include "common/CivilDate.h"
#include "common/Config.h"
#include "tesla/TeslaBle.h"

namespace solar {

namespace {

HttpResponse text(int status, const std::string& body) {
    HttpResponse response;
    response.status = status;
    response.body = body;
    return response;
}

/** A whole number, optionally negative, with surrounding spaces ignored; empty for anything else. */
std::optional<int> parseWholeNumber(const std::string& value) {
    size_t begin = value.find_first_not_of(' ');
    if (begin == std::string::npos) return std::nullopt;
    size_t end = value.find_last_not_of(' ');
    std::string trimmed = value.substr(begin, end - begin + 1);

    size_t digitsFrom = (trimmed[0] == '-' || trimmed[0] == '+') ? 1 : 0;
    if (digitsFrom >= trimmed.size() || trimmed.size() - digitsFrom > 9) return std::nullopt;
    for (size_t i = digitsFrom; i < trimmed.size(); i++) {
        if (!std::isdigit(static_cast<unsigned char>(trimmed[i]))) return std::nullopt;
    }
    return std::atoi(trimmed.c_str());
}

/** The "offset" query parameter of a paged list: 0 when absent, empty if it is not a whole number from 0 to 1000. */
static std::optional<size_t> pageOffset(const std::map<std::string, std::string>& query) {
    auto it = query.find("offset");
    if (it == query.end()) return size_t(0);
    auto parsed = parseWholeNumber(it->second);
    if (!parsed || *parsed < 0 || *parsed > 1000) return std::nullopt;
    return static_cast<size_t>(*parsed);
}

const std::string* find(const ApiHandler::Params& query, const char* key) {
    auto it = query.find(key);
    return it == query.end() ? nullptr : &it->second;
}

}  // namespace

HttpResponse ApiHandler::handle(const std::string& method, const std::string& path, const Params& query, bool hasSafeHeader) {
    // A web page on another site can send a POST without any permission, but it cannot add this header: so changing
    // requests without it are refused (the dashboard adds it).
    if (method == "POST" && !hasSafeHeader) {
        return text(403, "Blocked: changing requests need the X-WattSync header (the dashboard adds it). With curl: -H 'X-WattSync: 1'");
    }
    if (path == "/api/telemetry" && method == "GET") return telemetry();
    if (path == "/api/mode" && method == "POST") return updateMode(query);
    if (path == "/api/allowance" && method == "POST") return updateAllowance(query);
    if (path == "/api/daylight" && method == "POST") return updateDaylight(query);
    if (path == "/api/pause" && method == "POST") return updatePause(query);
    if (path == "/api/rate" && method == "POST") return updateRate(query);
    if (path == "/api/maxamps" && method == "POST") return updateMaxAmps(query);
    if (path == "/api/goal" && method == "POST") return updateGoal(query);
    if (path == "/api/notify" && method == "POST") return updateNotify(query);
    if (path == "/api/notify/test" && method == "POST") return testNotify();
    if (path == "/api/history" && method == "GET") return history(query);
    if (path == "/api/days" && method == "GET") return days(query);
    if (path == "/api/sessions" && method == "GET") return sessions(query);
    if (path == "/api/events" && method == "GET") return events(query);
    if (path == "/api/diagnostics" && method == "GET") return diagnostics();
    if (path == "/api/export.csv" && method == "GET") return exportCsv();
    if (path.rfind("/api/car/", 0) == 0 && method == "POST") return car(path, query);
    return text(404, "Not found");
}

HttpResponse ApiHandler::updateMode(const Params& query) {
    const std::string* requested = find(query, "mode");
    if (!requested) return text(400, "Missing mode.");
    auto mode = parseChargingMode(*requested);
    if (!mode) return text(400, "Unknown mode: " + *requested);

    // Check the current before changing anything, so a bad value never half-applies.
    std::optional<int> fastAmps;
    const std::string* ampsParam = find(query, "amps");
    if (*mode == ChargingMode::Fast && ampsParam) {
        fastAmps = parseWholeNumber(*ampsParam);
        if (!fastAmps || *fastAmps < config::kMinAmps || *fastAmps > controller_.state().maxAmps()) {
            return text(400, "Current must be a whole number from " + std::to_string(config::kMinAmps) + " to " +
                                 std::to_string(controller_.state().maxAmps()) + " A.");
        }
    }
    if (fastAmps) controller_.setFastAmps(*fastAmps);
    controller_.setMode(*mode);
    return text(200, "Mode updated");
}

HttpResponse ApiHandler::updateAllowance(const Params& query) {
    const std::string* watts = find(query, "watts");
    std::optional<int> value = watts ? parseWholeNumber(*watts) : std::nullopt;
    if (!value || !controller_.setGridAllowance(*value)) {
        return text(400, "watts must be a whole number from 0 to " + std::to_string(config::kMaxGridAllowanceWatts));
    }
    return text(200, "Allowance updated");
}

HttpResponse ApiHandler::updateDaylight(const Params& query) {
    const std::string* enabled = find(query, "enabled");
    const std::string* startParam = find(query, "startMin");
    const std::string* endParam = find(query, "endMin");
    if (!enabled && !startParam && !endParam) return text(400, "Give enabled, startMin or endMin.");
    if (enabled && *enabled != "true" && *enabled != "false") return text(400, "enabled must be true or false");

    // Check everything before changing anything, so a bad value never half-applies.
    const SystemState& state = controller_.state();
    int startMin = state.daylightStartMinutes(), endMin = state.daylightEndMinutes();
    bool marginsGiven = startParam || endParam;
    if (marginsGiven) {
        auto start = startParam ? parseWholeNumber(*startParam) : std::optional<int>(startMin);
        auto end = endParam ? parseWholeNumber(*endParam) : std::optional<int>(endMin);
        if (!start || !end || *start < 0 || *start > config::kMaxDaylightMarginMinutes || *end < 0 ||
            *end > config::kMaxDaylightMarginMinutes) {
            return text(400, "startMin and endMin must be whole numbers from 0 to " +
                                 std::to_string(config::kMaxDaylightMarginMinutes));
        }
        startMin = *start;
        endMin = *end;
    }
    if (enabled) controller_.setDaylightWindowEnabled(*enabled == "true");
    if (marginsGiven) controller_.setDaylightMargins(startMin, endMin);
    return text(200, "Daylight setting updated");
}

/**
 * False when the recorded money is missing or far below what the sun's energy is worth at the cheapest rate in use
 * (the energy began before prices were recorded, so only part of it was counted); the page then estimates it instead.
 */
static bool plausibleSaving(const SystemState& st, double savedCents, double sunWh) {
    if (sunWh < 1) return true;
    if (savedCents <= 0) return false;
    const double lowestRate = st.peakRateCents() > 0 ? std::min(st.rateCents(), st.peakRateCents()) : st.rateCents();
    return sunWh < 1000 || savedCents / (sunWh / 1000.0) >= lowestRate * 0.5;
}

/** Money saved, in dollars, or -1 when it was not (fully) recorded, so the page can estimate it. */
static double dollars(const SystemState& st, const DayTotals& t) {
    return plausibleSaving(st, t.savedCents, t.carSolarWh) ? std::round(t.savedCents) / 100.0 : -1;
}

HttpResponse ApiHandler::updateGoal(const Params& query) {
    const std::string* kwhParam = find(query, "kwh");
    const std::string* byParam = find(query, "byMinutes");
    std::optional<int> kwh = kwhParam ? parseWholeNumber(*kwhParam) : std::nullopt;
    std::optional<int> by = byParam ? parseWholeNumber(*byParam) : std::optional<int>(controller_.state().goalMinutes());
    if (!kwh || !by || !controller_.setGoal(*kwh, *by)) {
        return text(400, "kwh must be a whole number from 0 to " + std::to_string(config::kMaxGoalKwh) + " and byMinutes from 0 to 1439.");
    }
    return text(200, "Departure goal updated");
}

HttpResponse ApiHandler::updateNotify(const Params& query) {
    const std::string* url = find(query, "url");
    if (!url || !controller_.setNotifyUrl(*url)) {
        return text(400, "url must be empty or an http(s) address of at most " + std::to_string(Notifier::kMaxUrlLength) + " characters.");
    }
    return text(200, "Notifications updated");
}

HttpResponse ApiHandler::testNotify() {
    if (!notifier_) return text(502, "Could not send: notifications are not available");
    std::string problem = notifier_->sendTest();
    return problem.empty() ? text(200, "Sent") : text(502, "Could not send: " + problem);
}

HttpResponse ApiHandler::updateMaxAmps(const Params& query) {
    const std::string* amps = find(query, "amps");
    std::optional<int> value = amps ? parseWholeNumber(*amps) : std::nullopt;
    if (!value || !controller_.setMaxAmps(*value)) {
        return text(400, "amps must be a whole number from " + std::to_string(config::kMinAmps) + " to " +
                             std::to_string(config::kMaxFastAmps));
    }
    return text(200, "Maximum current updated");
}

HttpResponse ApiHandler::updateRate(const Params& query) {
    const std::string* centsParam = find(query, "cents");
    const std::string* peakParam = find(query, "peakCents");
    const std::string* fromParam = find(query, "peakStart");
    const std::string* toParam = find(query, "peakEnd");
    if (!centsParam && !peakParam && !fromParam && !toParam) return text(400, "Give cents, peakCents, peakStart or peakEnd.");
    // Check everything before changing anything, so a bad value never half-applies. A part that is not given keeps its value.
    const SystemState& state = controller_.state();
    auto pick = [&](const std::string* param, int current, int low, int high) -> std::optional<int> {
        if (!param) return current;
        auto parsed = parseWholeNumber(*param);
        if (!parsed || *parsed < low || *parsed > high) return std::nullopt;
        return parsed;
    };
    auto cents = pick(centsParam, state.rateCents(), 0, config::kMaxRateCents);
    auto peak = pick(peakParam, state.peakRateCents(), 0, config::kMaxRateCents);
    if (!cents || !peak) {
        return text(400, "cents and peakCents must be whole numbers from 0 to " + std::to_string(config::kMaxRateCents));
    }
    auto from = pick(fromParam, state.peakStartHour(), 0, 23);
    auto to = pick(toParam, state.peakEndHour(), 1, 24);
    if (!from || !to || *from == *to) return text(400, "peakStart must be 0 to 23 and peakEnd 1 to 24, and they must differ");
    controller_.setPrices(*cents, *peak, *from, *to);
    return text(200, "Price updated");
}

HttpResponse ApiHandler::history(const Params& query) {
    const int64_t now = platform_.epochSeconds();
    auto kwh = [](double wh) { return std::round(wh / 10.0) / 100.0; };   // Wh -> kWh, two decimals

    int daysAgo = 0;
    if (const std::string* value = find(query, "daysAgo")) {
        auto parsed = parseWholeNumber(*value);
        if (!parsed || *parsed < 0 || *parsed > 364) return text(400, "daysAgo must be a whole number from 0 to 364");
        daysAgo = *parsed;
    }

    JsonDocument doc;
    doc["now"] = now;
    doc["daysAgo"] = daysAgo;
    doc["today"] = history_ ? history_->localDay(now) : 0;
    JsonObject totals = doc["totals"].to<JsonObject>();
    totals["solarKwh"] = 0;
    totals["carKwh"] = 0;
    totals["carSolarKwh"] = 0;
    totals["importKwh"] = 0;
    totals["exportKwh"] = 0;
    totals["savedDollars"] = 0;

    int64_t dayStart = now;
    std::string samplesJson;
    if (history_) {
        // A past day is found by stepping back whole days from the middle of today (so daylight saving cannot slip a day).
        int64_t todayStart = history_->startOfLocalDay(now);
        int64_t target = daysAgo == 0 ? now : todayStart + 43200 - static_cast<int64_t>(daysAgo) * 86400;
        dayStart = history_->startOfLocalDay(target);
        int64_t dayEnd = daysAgo == 0 ? INT64_MAX : dayStart + 86399;
        int step = daysAgo <= 29 ? 300 : 3600;   // after 30 days only hourly records remain
        // Written straight into the reply text: a JSON document of about 300 rows needs far more memory than the text does,
        // and the board's largest free block is small once Bluetooth is in the build.
        samplesJson.reserve(20000);
        for (const HistorySample& s : history_->seriesRange(dayStart, dayEnd, step)) {
            if (!samplesJson.empty()) samplesJson += ',';
            samplesJson += '[';
            samplesJson += std::to_string(s.t) + ',' + std::to_string(s.solar) + ',' + std::to_string(s.home) + ',' + std::to_string(s.grid) + ',' +
                           std::to_string(s.car) + ',' + std::to_string(s.mode) + ',' + std::to_string(s.flags);
            samplesJson += ']';
        }
        DayTotals day = history_->totalsForDay(history_->localDay(target));
        totals["solarKwh"] = kwh(day.solarWh);
        totals["carKwh"] = kwh(day.carWh);
        totals["carSolarKwh"] = kwh(day.carSolarWh);
        totals["importKwh"] = kwh(day.importWh);
        totals["exportKwh"] = kwh(day.exportWh);
        totals["savedDollars"] = dollars(controller_.state(), day);
    }
    doc["dayStart"] = dayStart;
    int64_t windowStart = 0, windowEnd = 0;
    if (schedule_.windowFor(dayStart + 43200, windowStart, windowEnd)) {
        doc["daylight"]["start"] = windowStart;
        doc["daylight"]["end"] = windowEnd;
    } else {
        doc["daylight"] = nullptr;
    }

    HttpResponse response;
    response.contentType = "application/json";
    serializeJson(doc, response.body);
    // The document ends with "}": put the samples in before it.
    response.body.pop_back();
    response.body += ",\"samples\":[" + samplesJson + "]}";
    return response;
}

HttpResponse ApiHandler::days(const Params& query) {
    int n = 30;
    if (const std::string* value = find(query, "n")) {
        auto parsed = parseWholeNumber(*value);
        if (!parsed || *parsed < 1 || *parsed > 365) return text(400, "n must be a whole number from 1 to 365");
        n = *parsed;
    }
    auto kwh = [](double wh) { return std::round(wh / 10.0) / 100.0; };
    JsonDocument doc;
    JsonArray rows = doc["days"].to<JsonArray>();
    int64_t today = history_ ? history_->localDay(platform_.epochSeconds()) : 0;
    doc["today"] = today;
    if (history_) {
        for (const auto& entry : history_->allDays()) {
            if (entry.first <= today - n || entry.first > today) continue;
            JsonArray row = rows.add<JsonArray>();
            row.add(entry.first);
            row.add(kwh(entry.second.solarWh));
            row.add(kwh(entry.second.carWh));
            row.add(kwh(entry.second.carSolarWh));
            row.add(kwh(entry.second.importWh));
            row.add(kwh(entry.second.exportWh));
            row.add(dollars(controller_.state(), entry.second));
        }
    }
    HttpResponse response;
    response.contentType = "application/json";
    serializeJson(doc, response.body);
    return response;
}

HttpResponse ApiHandler::sessions(const Params& query) {
    int n = 10;
    if (const std::string* value = find(query, "n")) {
        auto parsed = parseWholeNumber(*value);
        if (!parsed || *parsed < 1 || *parsed > 100) return text(400, "n must be a whole number from 1 to 100");
        n = *parsed;
    }
    auto offset = pageOffset(query);
    if (!offset) return text(400, "offset must be a whole number from 0 to 1000");
    JsonDocument doc;
    JsonArray rows = doc["sessions"].to<JsonArray>();
    doc["open"] = nullptr;
    doc["total"] = history_ ? static_cast<int>(history_->sessionCount()) : 0;
    auto fill = [&](JsonObject out, const HistorySession& x, int64_t endAt) {
        double energyWh = x.energyWh();
        double hours = std::max(1.0, static_cast<double>(endAt - x.start)) / 3600.0;
        out["start"] = x.start;
        out["end"] = x.end;
        out["kwh"] = std::round(energyWh / 10.0) / 100.0;
        out["sunPct"] = x.estimatedWh > 1 ? static_cast<int>(std::round(std::min(1.0, x.sunWh / x.estimatedWh) * 100)) : 0;
        out["sunKwh"] = std::round(std::min(x.sunWh, x.estimatedWh) / 10.0) / 100.0;
        out["avgAmps"] = std::round(energyWh / hours / config::kHouseholdVoltage * 10) / 10.0;
        out["reason"] = x.reason;
        // Money recorded for only part of the session (it began before prices were recorded) would read far too low:
        // treat a price well under the cheapest rate in use as "not recorded" so the page estimates it instead.
        out["saved"] = plausibleSaving(controller_.state(), x.savedCents, x.sunWh) ? std::round(x.savedCents) / 100.0 : -1.0;
        out["mode"] = x.mode == 0 ? "ECO" : x.mode == 1 ? "MAX_SOLAR" : x.mode == 2 ? "FAST" : "";
        out["modeMixed"] = x.mixed;
    };
    if (history_) {
        for (const HistorySession& x : history_->recentSessions(n, *offset)) fill(rows.add<JsonObject>(), x, x.end);
        if (const HistorySession* open = history_->openSession()) fill(doc["open"].to<JsonObject>(), *open, platform_.epochSeconds());
    }
    HttpResponse response;
    response.contentType = "application/json";
    serializeJson(doc, response.body);
    return response;
}

HttpResponse ApiHandler::events(const Params& query) {
    int n = 5;
    if (const std::string* value = find(query, "n")) {
        auto parsed = parseWholeNumber(*value);
        if (!parsed || *parsed < 1 || *parsed > 20) return text(400, "n must be a whole number from 1 to 20");
        n = *parsed;
    }
    auto offset = pageOffset(query);
    if (!offset) return text(400, "offset must be a whole number from 0 to 1000");
    JsonDocument doc;
    JsonArray rows = doc["events"].to<JsonArray>();
    doc["total"] = history_ ? static_cast<int>(history_->eventCount()) : 0;
    if (history_) {
        for (const HistoryEvent& e : history_->recentEvents(n, *offset)) {
            JsonObject entry = rows.add<JsonObject>();
            entry["t"] = e.t;
            entry["text"] = e.text;
        }
    }
    HttpResponse response;
    response.contentType = "application/json";
    serializeJson(doc, response.body);
    return response;
}

HttpResponse ApiHandler::diagnostics() {
    if (!diagnostics_) return text(404, "Not available");
    const Diagnostics d = diagnostics_->read();
    JsonDocument doc;
    doc["platform"] = d.platform;
    doc["now"] = platform_.epochSeconds();
    doc["uptimeSeconds"] = d.uptimeSeconds;
    doc["lastRestart"]["reason"] = d.lastRestart;
    doc["lastRestart"]["crash"] = d.lastRestartWasCrash;
    if (d.restarts >= 0) doc["restarts"] = d.restarts;
    if (d.crashes >= 0) doc["crashes"] = d.crashes;
    if (!d.lastCrashReason.empty()) {
        doc["lastCrash"]["reason"] = d.lastCrashReason;
        doc["lastCrash"]["at"] = d.lastCrashAt;
    }
    if (d.freeHeapKb >= 0) {
        doc["memory"]["freeKb"] = d.freeHeapKb;
        doc["memory"]["minFreeKb"] = d.minFreeHeapKb;
        doc["memory"]["largestBlockKb"] = d.largestBlockKb;
    }
    if (d.hasWifi) doc["wifiRssi"] = d.wifiRssi;
    if (!d.firmwareBuilt.empty()) doc["firmwareBuilt"] = d.firmwareBuilt;
    doc["clockSet"] = d.clockSet;
    HttpResponse response;
    response.contentType = "application/json";
    serializeJson(doc, response.body);
    return response;
}

HttpResponse ApiHandler::exportCsv() {
    HttpResponse response;
    response.contentType = "text/csv";
    response.downloadName = "wattsync-days.csv";
    response.body = "date,solar_kwh,car_kwh,car_from_sun_kwh,grid_import_kwh,grid_export_kwh,saved_dollars\n";
    if (!history_) return response;
    char line[120];
    for (const auto& entry : history_->allDays()) {
        int year;
        unsigned month, day;
        civilFromDays(entry.first, year, month, day);
        const DayTotals& t = entry.second;
        char saved[16] = "";
        if (t.savedCents > 0 && plausibleSaving(controller_.state(), t.savedCents, t.carSolarWh)) std::snprintf(saved, sizeof saved, "%.2f", t.savedCents / 100);
        std::snprintf(line, sizeof line, "%04d-%02u-%02u,%.2f,%.2f,%.2f,%.2f,%.2f,%s\n", year, month, day, t.solarWh / 1000,
                      t.carWh / 1000, t.carSolarWh / 1000, t.importWh / 1000, t.exportWh / 1000, saved);
        response.body += line;
    }
    return response;
}

HttpResponse ApiHandler::updatePause(const Params& query) {
    const std::string* enabled = find(query, "enabled");
    if (!enabled || (*enabled != "true" && *enabled != "false")) return text(400, "enabled must be true or false");
    const bool pause = *enabled == "true";
    controller_.setPaused(pause);
    return text(200, pause ? "Charging paused" : "Charging resumed");
}

namespace {

void addCarToTelemetry(JsonObject root, const CarControl* car, const ChargeRouter* router) {
    JsonObject out = root["car"].to<JsonObject>();
    if (!car) {
        out["supported"] = false;
        return;
    }
    const CarSnapshot s = car->snapshot();
    out["supported"] = s.supported;
    if (!s.supported) return;
    out["paired"] = s.paired;
    out["vin"] = s.vin;                 // masked
    out["key"] = s.key;
    out["watch"] = s.watch;
    out["busy"] = s.busy;
    out["job"] = s.job;
    if (s.busy && s.job == "pair") {
        JsonObject pair = out["pair"].to<JsonObject>();
        pair["state"] = s.pairState;
        pair["text"] = s.pairText;
        pair["secondsLeft"] = s.pairSecondsLeft;
    }
    if (s.hasResult) {
        JsonObject result = out["result"].to<JsonObject>();
        result["job"] = s.resultJob;
        result["ok"] = s.resultOk;
        result["ageSeconds"] = s.resultAgeSeconds;
        JsonArray lines = result["lines"].to<JsonArray>();
        for (const std::string& line : s.resultLines) lines.add(line);
    }
    if (s.hasReading) {
        JsonObject reading = out["reading"].to<JsonObject>();
        reading["ageSeconds"] = s.readingAgeSeconds;
        reading["sleep"] = s.sleep;
        reading["lock"] = s.lock;
        const tesla::ChargeState& c = s.charge;
        if (c.valid) {
            reading["charging"] = tesla::chargingStateText(c.chargingState);
            reading["chargingCode"] = c.chargingState;
        }
        if (c.hasBatteryLevel) reading["battery"] = c.batteryLevel;
        if (c.hasChargeLimit) reading["limit"] = c.chargeLimit;
        if (c.hasActualCurrent) reading["amps"] = c.actualCurrent;
        if (c.hasCurrentRequest) reading["requestAmps"] = c.currentRequest;
        if (c.hasCurrentRequestMax) reading["maxAmps"] = c.currentRequestMax;
        if (c.hasVoltage) reading["volts"] = c.voltage;
        if (c.hasPower) reading["kw"] = c.power;
    }
    out["restoreAmps"] = s.restoreAmps;
    if (router) {
        const CarCommander::Status st = router->commander().status();
        JsonObject control = out["control"].to<JsonObject>();
        control["through"] = router->throughCar();
        control["state"] = st.state;
        control["unreachableSeconds"] = st.unreachableSeconds;
        control["problem"] = st.problem;
        const CarOnReturn policy = router->commander().onReturn();
        control["onReturn"] = policy == CarOnReturn::Tell ? "tell" : policy == CarOnReturn::Stop ? "stop" : "keep";
        control["allowWake"] = st.allowWake;
        control["lastWakeAgeSeconds"] = st.lastWakeAgeSeconds;
    }
}

/** Spaces and dashes are tolerated and the letters are made capital; whatever is left is checked as a VIN. */
std::string cleanVin(const std::string& raw) {
    std::string vin;
    for (char c : raw) {
        if (c == ' ' || c == '-' || c == '\t') continue;
        vin += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return vin;
}

}  // namespace

HttpResponse ApiHandler::car(const std::string& path, const Params& query) {
    if (!car_) return text(400, "Car control needs the ESP32 board.");
    const CarSnapshot now = car_->snapshot();
    if (!now.supported) return text(400, "Car control needs the ESP32 board.");

    auto answer = [](const CarResult& r, const char* ok) {
        if (r.ok) return text(200, ok);
        return text(r.busy ? 409 : 400, r.message);
    };

    if (path == "/api/car/pair") {
        const std::string* raw = find(query, "vin");
        std::string vin = raw ? cleanVin(*raw) : std::string();
        // The VIN is never echoed back, not even in an error.
        if (!tesla::isValidVin(vin)) return text(400, "That is not a VIN (17 letters and digits, no I, O or Q).");
        return answer(car_->startPair(vin), "Pairing started");
    }
    if (path == "/api/car/refresh") {
        if (!now.paired) return text(400, "No car is paired yet.");
        return answer(car_->startRefresh(), "Reading the car");
    }
    if (path == "/api/car/watch") {
        const std::string* enabled = find(query, "enabled");
        if (!enabled || (*enabled != "true" && *enabled != "false")) return text(400, "enabled must be true or false");
        car_->setWatch(*enabled == "true");
        return text(200, "Car watching updated");
    }
    if (path == "/api/car/through") {
        if (!router_) return text(400, "Car control needs the ESP32 board.");
        const std::string* mode = find(query, "mode");
        if (!mode || (*mode != "charger" && *mode != "car")) return text(400, "mode must be charger or car");
        if (*mode == "car") {
            if (!now.paired) return text(400, "No car is paired yet.");
            const std::string* confirm = find(query, "confirm");
            if (!confirm || *confirm != "yes") return text(400, "Switching to the car needs confirm=yes: WattSync will then change the car's charging by itself.");
        }
        return answer(router_->setThroughCar(*mode == "car"), *mode == "car" ? "The car is in command" : "The charger is in command");
    }
    if (path == "/api/car/policy") {
        if (!router_) return text(400, "Car control needs the ESP32 board.");
        const std::string* value = find(query, "onReturn");
        if (!value || (*value != "keep" && *value != "tell" && *value != "stop")) return text(400, "onReturn must be keep, tell or stop");
        router_->setOnReturn(*value == "tell" ? CarOnReturn::Tell : *value == "stop" ? CarOnReturn::Stop : CarOnReturn::Keep);
        return text(200, "Saved");
    }
    if (path == "/api/car/wake") {
        if (!now.paired) return text(400, "No car is paired yet.");
        return answer(car_->startWake(), "Waking the car");
    }
    if (path == "/api/car/allowwake") {
        if (!router_) return text(400, "Car control needs the ESP32 board.");
        const std::string* enabled = find(query, "enabled");
        if (!enabled || (*enabled != "true" && *enabled != "false")) return text(400, "enabled must be true or false");
        router_->setAllowWake(*enabled == "true");
        return text(200, "Saved");
    }
    if (path == "/api/car/cancel") {
        car_->cancel();
        return text(200, "Stopped");
    }
    if (path == "/api/car/forget") {
        car_->forget();
        return text(200, "Car forgotten");
    }
    if (path == "/api/car/test") {
        const std::string* action = find(query, "action");
        if (!action) return text(400, "Missing action.");
        if (*action != "start" && *action != "stop" && *action != "amps") return text(400, "action must be start, stop or amps");
        if (!now.paired) return text(400, "No car is paired yet.");
        if (*action == "amps") {
            const std::string* amps = find(query, "amps");
            std::optional<int> value = amps ? parseWholeNumber(*amps) : std::nullopt;
            if (!value || *value < config::kMinAmps || *value > config::kMaxFastAmps) {
                return text(400, "amps must be a whole number from " + std::to_string(config::kMinAmps) + " to " + std::to_string(config::kMaxFastAmps) + ".");
            }
            return answer(car_->startSetAmps(*value), "Setting the car's current");
        }
        return answer(*action == "start" ? car_->startStart() : car_->startStop(), *action == "start" ? "Starting the charge" : "Stopping the charge");
    }
    return text(404, "Not found");
}

HttpResponse ApiHandler::telemetry() {
    const SystemState& state = controller_.state();
    const auto& control = state.control();
    const double volts = config::kHouseholdVoltage;
    const double chargerAmps = wallbox_.currentDrawAmps();
    const double chargerWatts = chargerAmps * volts;
    const std::optional<int> limit = wallbox_.currentLimitAmps();
    const std::optional<ChargerStatus> chargerStatus = wallbox_.primaryStatus();
    const int64_t epoch = platform_.epochSeconds();
    const bool connected = wallbox_.hasConnectedChargers();

    JsonDocument doc;
    doc["currentMode"] = toString(state.mode());
    doc["paused"] = state.paused();
    doc["rateCents"] = state.rateCents();
    doc["peakRateCents"] = state.peakRateCents();
    doc["peakStartHour"] = state.peakStartHour();
    doc["peakEndHour"] = state.peakEndHour();
    doc["fastModeAmps"] = state.fastModeAmps();
    doc["maxAmps"] = state.maxAmps();
    doc["notifyTo"] = Notifier::maskedHost(state.notifyUrl());
    JsonObject goal = doc["goal"].to<JsonObject>();
    goal["kwh"] = state.goalKwh();
    goal["byMinutes"] = state.goalMinutes();
    goal["doneKwh"] = std::round(state.goalDoneWh() / 100.0) / 10.0;
    goal["forcing"] = state.goalForcing();
    goal["forcedAmps"] = state.goalForcedAmps();
    doc["gridAllowanceWatts"] = state.gridAllowanceWatts();
    doc["chargerConnected"] = connected;
    doc["enphaseOk"] = state.enphaseOk();
    doc["enphaseMessage"] = state.enphaseMessage();

    JsonObject daylight = doc["daylight"].to<JsonObject>();
    daylight["enabled"] = state.daylightWindowEnabled();
    daylight["open"] = !state.daylightWindowEnabled() || schedule_.isOpen(epoch);
    daylight["window"] = schedule_.describe(epoch);
    daylight["startMinutes"] = state.daylightStartMinutes();
    daylight["endMinutes"] = state.daylightEndMinutes();

    JsonObject power = doc["power"].to<JsonObject>();
    power["voltage"] = volts;
    power["solarWatts"] = state.solarWatts();
    power["gridWatts"] = control ? control->gridWatts : 0.0;   // + import, - export
    power["chargerWatts"] = chargerWatts;
    power["homeWatts"] = control ? std::max(0.0, state.solarWatts() + control->gridWatts - chargerWatts) : 0.0;

    JsonObject charger = doc["charger"].to<JsonObject>();
    charger["connected"] = connected;
    // The dashboard expects the Java enum names (CHARGING, SUSPENDED_EV, ...), in the order of ChargerStatus.
    static const char* const names[] = {"AVAILABLE", "PREPARING", "CHARGING", "SUSPENDED_EV", "SUSPENDED_EVSE",
                                        "FINISHING", "RESERVED", "UNAVAILABLE", "FAULTED", "UNKNOWN"};
    charger["status"] = chargerStatus ? names[static_cast<int>(*chargerStatus)] : "NONE";
    charger["amps"] = chargerAmps;
    charger["limitAmps"] = limit.value_or(0);
    charger["restored"] = wallbox_.anyRestored();
    charger["drawMeasured"] = wallbox_.drawIsMeasured();
    charger["lastCommand"] = wallbox_.lastCommand();
    charger["lastCommandAt"] = wallbox_.lastCommandAtSeconds();   // epoch seconds, 0 if none yet

    JsonObject ctl = doc["controller"].to<JsonObject>();
    ctl["hasReading"] = control.has_value();
    ctl["availableAmps"] = control ? control->availableAmps : 0.0;
    ctl["availableWatts"] = control ? control->availableAmps * volts : 0.0;
    ctl["smoothedAmps"] = control ? control->smoothedAmps : 0.0;
    ctl["targetAmps"] = control ? control->targetAmps : 0;
    ctl["reason"] = control ? control->reason : std::string("waiting for first reading");
    ctl["readingAgeSeconds"] =
        control ? static_cast<int64_t>((platform_.nowMillis() - control->readingAtMillis) / 1000) : int64_t(-1);
    // ECO and MAX_SOLAR both use the allowance; Custom (FAST) ignores it.
    ctl["gridAllowanceWatts"] = state.mode() != ChargingMode::Fast ? state.gridAllowanceWatts() : 0;
    ctl["failSafe"] = state.failSafe();
    ctl["pollSeconds"] = config::kPollIntervalSeconds;
    addCarToTelemetry(doc.as<JsonObject>(), car_, router_);

    HttpResponse response;
    response.contentType = "application/json";
    serializeJson(doc, response.body);
    return response;
}

}  // namespace solar
