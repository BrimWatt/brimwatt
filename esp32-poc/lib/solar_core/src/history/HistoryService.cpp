#include "history/HistoryService.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/CivilDate.h"

namespace solar {

namespace {

constexpr uint32_t kMaxGapSeconds = 300;   // longer gaps (no readings) are not counted as energy
constexpr uint32_t kEventRepeatSeconds = 120;

int16_t clampWatts(double w) { return static_cast<int16_t>(std::max(-32000.0, std::min(32000.0, std::round(w)))); }

std::vector<std::string> split(const std::string& line, char sep) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t end = line.find(sep, start);
        if (end == std::string::npos) {
            parts.push_back(line.substr(start));
            return parts;
        }
        parts.push_back(line.substr(start, end - start));
        start = end + 1;
    }
}

}  // namespace

int64_t HistoryService::localDay(int64_t epoch) const { return floorDiv(epoch + platform_.utcOffsetSeconds(epoch), 86400); }

int64_t HistoryService::startOfLocalDay(int64_t epoch) const {
    int64_t day = localDay(epoch);
    return day * 86400 - platform_.utcOffsetSeconds(epoch);
}

void HistoryService::recordSample(double solarWatts, double gridWatts, double carWatts, ChargingMode mode, bool paused) {
    int64_t now = platform_.epochSeconds();
    if (now < kMinValidEpoch) return;
    solarWatts = std::max(0.0, solarWatts);
    carWatts = std::max(0.0, carWatts);
    double homeWatts = std::max(0.0, solarWatts + gridWatts - carWatts);

    HistorySample sample;
    sample.t = static_cast<uint32_t>(now);
    sample.solar = clampWatts(solarWatts);
    sample.home = clampWatts(homeWatts);
    sample.grid = clampWatts(gridWatts);
    sample.car = clampWatts(carWatts);
    sample.mode = mode == ChargingMode::Eco ? 0 : mode == ChargingMode::MaxSolar ? 1 : 2;
    sample.flags = static_cast<uint8_t>((carWatts >= 100 ? kFlagCharging : 0) | (paused ? kFlagPaused : 0));

    // Energy since the previous reading, counted into the local day it ended in.
    if (lastSampleAt_ > 0 && sample.t > lastSampleAt_ && sample.t - lastSampleAt_ <= kMaxGapSeconds) {
        double hours = (sample.t - lastSampleAt_) / 3600.0;
        double carFromSun = std::min(carWatts, std::max(0.0, solarWatts - homeWatts));
        DayTotals& day = days_[localDay(now)];
        day.solarWh += solarWatts * hours;
        day.carWh += carWatts * hours;
        day.carSolarWh += carFromSun * hours;
        const double savedNow = carFromSun * hours / 1000.0 * priceAt_(now);
        day.savedCents += savedNow;
        day.importWh += std::max(0.0, gridWatts) * hours;
        day.exportWh += std::max(0.0, -gridWatts) * hours;
        if (open_) {
            open_->estimatedWh += carWatts * hours;
            open_->sunWh += carFromSun * hours;
            open_->savedCents += savedNow;
            if (open_->mode < 0) open_->mode = sample.mode;
            else if (open_->mode != sample.mode) open_->mixed = true;
            dirty_ = journalDirty_ = true;
        }
        while (days_.size() > kMaxDays) days_.erase(days_.begin());
    }
    lastSampleAt_ = sample.t;

    samples_.push_back(sample);
    while (samples_.size() > kMaxSamples) samples_.pop_front();
    feedFiveMinute(sample);
    dirty_ = journalDirty_ = true;
}

HistorySample HistoryService::average(const Accum& a, int64_t middle) {
    HistorySample s;
    s.t = static_cast<uint32_t>(middle);
    s.solar = clampWatts(a.solar / a.n);
    s.home = clampWatts(a.home / a.n);
    s.grid = clampWatts(a.grid / a.n);
    s.car = clampWatts(a.car / a.n);
    s.flags = a.flags;
    s.mode = a.mode;
    return s;
}

void HistoryService::feedFiveMinute(const HistorySample& s) {
    int64_t bucket = s.t / 300;
    if (acc5_.n > 0 && acc5_.bucket != bucket) flushFiveMinute();
    acc5_.bucket = bucket;
    acc5_.solar += s.solar; acc5_.home += s.home; acc5_.grid += s.grid; acc5_.car += s.car;
    acc5_.flags |= s.flags;
    acc5_.mode = s.mode;
    acc5_.n++;
}

void HistoryService::flushFiveMinute() {
    if (acc5_.n == 0) return;
    HistorySample record = average(acc5_, acc5_.bucket * 300 + 150);
    acc5_ = Accum();
    fiveMin_.push(record);
    if (pendingFiveMin_ < kMaxFiveMinute) pendingFiveMin_++;
    feedHourly(record);
}

void HistoryService::feedHourly(const HistorySample& r) {
    int64_t bucket = r.t / 3600;
    if (acc60_.n > 0 && acc60_.bucket != bucket) flushHourly();
    acc60_.bucket = bucket;
    acc60_.solar += r.solar; acc60_.home += r.home; acc60_.grid += r.grid; acc60_.car += r.car;
    acc60_.flags |= r.flags;
    acc60_.mode = r.mode;
    acc60_.n++;
}

void HistoryService::flushHourly() {
    if (acc60_.n == 0) return;
    hourly_.push(average(acc60_, acc60_.bucket * 3600 + 1800));
    if (pendingHourly_ < kMaxHourly) pendingHourly_++;
    acc60_ = Accum();
}

void HistoryService::sessionStarted(double meterStartWh) {
    int64_t now = platform_.epochSeconds();
    if (now < kMinValidEpoch) return;
    if (open_) {   // we never saw the previous one end (a lost message, or a restart in between): close it where it stands
        open_->end = static_cast<uint32_t>(now);
        open_->reason = "unknown";
        sessions_.push_back(*open_);
        while (sessions_.size() > kMaxSessions) sessions_.pop_front();
    }
    HistorySession session;
    session.start = static_cast<uint32_t>(now);
    session.meterStartWh = meterStartWh;
    open_ = session;
    dirty_ = journalDirty_ = true;
}

void HistoryService::sessionStopped(double meterStopWh, const std::string& reason) {
    int64_t now = platform_.epochSeconds();
    if (!open_ || now < kMinValidEpoch) return;
    HistorySession done = *open_;
    open_.reset();
    done.end = static_cast<uint32_t>(now);
    if (done.meterStartWh >= 0 && meterStopWh >= done.meterStartWh) done.meterWh = meterStopWh - done.meterStartWh;
    std::string clean = reason;
    for (char& c : clean) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    done.reason = clean;
    sessions_.push_back(done);
    while (sessions_.size() > kMaxSessions) sessions_.pop_front();
    dirty_ = journalDirty_ = true;

    double kwh = done.energyWh() / 1000;
    char text[96];
    if (done.estimatedWh > 1) {
        std::snprintf(text, sizeof text, "Charging ended: %.1f kWh, %d%% from the sun", kwh,
                      static_cast<int>(std::round(std::min(1.0, done.sunWh / done.estimatedWh) * 100)));
    } else {
        std::snprintf(text, sizeof text, "Charging ended: %.1f kWh", kwh);
    }
    addEvent(text);
}

std::vector<HistorySession> HistoryService::recentSessions(size_t limit, size_t offset) const {
    std::vector<HistorySession> out;
    size_t skipped = 0;
    for (auto it = sessions_.rbegin(); it != sessions_.rend() && out.size() < limit; ++it) {
        if (skipped++ < offset) continue;
        out.push_back(*it);
    }
    return out;
}

void HistoryService::addEvent(const std::string& text) {
    int64_t now = platform_.epochSeconds();
    if (now < kMinValidEpoch) return;
    // Only an identical line right after the previous one is dropped (a flapping status). The same line after a different
    // one is real, such as switching Eco, Custom, Eco: dropping it would leave the log contradicting the state.
    if (!events_.empty() && now - events_.back().t < kEventRepeatSeconds && events_.back().text == text) return;
    events_.push_back({static_cast<uint32_t>(now), text});
    while (events_.size() > kMaxEvents) events_.pop_front();
    dirty_ = journalDirty_ = true;
    if (eventListener_) eventListener_(text);
}

std::vector<HistorySample> HistoryService::series(int64_t from, int stepSeconds) const {
    return seriesRange(from, INT64_MAX, stepSeconds);
}

std::vector<HistorySample> HistoryService::seriesRange(int64_t from, int64_t to, int stepSeconds) const {
    // Each moment comes from the finest tier that still has it: minutes, else 5 minutes, else hours.
    int64_t oldestMinute = samples_.empty() ? INT64_MAX : samples_.front().t;
    int64_t oldestFive = fiveMin_.size() == 0 ? INT64_MAX : fiveMin_.at(0).t;
    std::vector<HistorySample> raw;
    // A coarser record is used only if it lies wholly before the finer data (it covers 150 s / 1800 s either side of t).
    for (size_t i = 0; i < hourly_.size(); i++) {
        const HistorySample& r = hourly_.at(i);
        if (r.t >= from && r.t <= to && r.t + 1800 <= oldestFive - 150 && r.t + 1800 <= oldestMinute) raw.push_back(r);
    }
    for (size_t i = 0; i < fiveMin_.size(); i++) {
        const HistorySample& r = fiveMin_.at(i);
        if (r.t >= from && r.t <= to && r.t + 150 <= oldestMinute) raw.push_back(r);
    }
    for (const auto& s : samples_) if (s.t >= from && s.t <= to) raw.push_back(s);
    return bucketed(raw, from, stepSeconds);
}

std::vector<HistorySample> HistoryService::bucketed(const std::vector<HistorySample>& raw, int64_t from, int stepSeconds) {
    if (stepSeconds <= 60) return raw;
    std::vector<HistorySample> out;
    Accum sum;
    for (const auto& s : raw) {
        int64_t bucket = (static_cast<int64_t>(s.t) - from) / stepSeconds;
        if (sum.n > 0 && bucket != sum.bucket) {
            out.push_back(average(sum, from + sum.bucket * stepSeconds + stepSeconds / 2));
            sum = Accum();
        }
        sum.bucket = bucket;
        sum.solar += s.solar; sum.home += s.home; sum.grid += s.grid; sum.car += s.car;
        sum.flags |= s.flags;
        sum.mode = s.mode;
        sum.n++;
    }
    if (sum.n > 0) out.push_back(average(sum, from + sum.bucket * stepSeconds + stepSeconds / 2));
    return out;
}

DayTotals HistoryService::totalsForDay(int64_t epochDay) const {
    auto it = days_.find(epochDay);
    return it == days_.end() ? DayTotals() : it->second;
}

std::vector<HistoryEvent> HistoryService::recentEvents(size_t limit, size_t offset) const {
    std::vector<HistoryEvent> out;
    size_t skipped = 0;
    for (auto it = events_.rbegin(); it != events_.rend() && out.size() < limit; ++it) {
        if (skipped++ < offset) continue;
        out.push_back(*it);
    }
    return out;
}

// ---------------------------------------------------------------- saving

void HistoryService::forEachLine(const std::function<void(const std::string&)>& write) const {
    char buffer[200];
    for (const auto& d : days_) {
        std::snprintf(buffer, sizeof buffer, "D\t%lld\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.2f", (long long)d.first, d.second.solarWh,
                      d.second.carWh, d.second.carSolarWh, d.second.importWh, d.second.exportWh, d.second.savedCents);
        write(buffer);
    }
    auto sessionLine = [&](char tag, const HistorySession& x) {
        std::snprintf(buffer, sizeof buffer, "%c\t%u\t%u\t%.1f\t%.1f\t%.1f\t%.1f\t", tag, (unsigned)x.start, (unsigned)x.end,
                      x.meterStartWh, x.meterWh, x.estimatedWh, x.sunWh);
        char saved[40];
        std::snprintf(saved, sizeof saved, "\t%.2f\t%d\t%d", x.savedCents, x.mode, x.mixed ? 1 : 0);   // after the reason, so older readers still find it
        write(std::string(buffer) + x.reason + saved);
    };
    for (const auto& x : sessions_) sessionLine('T', x);
    if (open_) sessionLine('O', *open_);
    for (const auto& e : events_) write("E\t" + std::to_string(e.t) + "\t" + e.text);
    for (const auto& s : samples_) {
        std::snprintf(buffer, sizeof buffer, "S\t%u\t%d\t%d\t%d\t%d\t%u\t%u", (unsigned)s.t, s.solar, s.home, s.grid, s.car,
                      (unsigned)s.mode, (unsigned)s.flags);
        write(buffer);
    }
}

void HistoryService::markClean() {
    dirty_ = false;
    journalDirty_ = false;
    mainSavedAt_ = std::max<int64_t>(platform_.epochSeconds(), lastSampleAt_);
}

void HistoryService::forEachJournalLine(const std::function<void(const std::string&)>& write) const {
    // Only what happened since the last full save, so the journal stays a few kilobytes.
    char buffer[200];
    auto since = [&](uint32_t t) { return static_cast<int64_t>(t) >= mainSavedAt_ - 60; };
    for (const auto& d : days_) {
        if (d.first < localDay(mainSavedAt_)) continue;
        std::snprintf(buffer, sizeof buffer, "D\t%lld\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.2f", (long long)d.first, d.second.solarWh,
                      d.second.carWh, d.second.carSolarWh, d.second.importWh, d.second.exportWh, d.second.savedCents);
        write(buffer);
    }
    auto sessionLine = [&](char tag, const HistorySession& x) {
        std::snprintf(buffer, sizeof buffer, "%c\t%u\t%u\t%.1f\t%.1f\t%.1f\t%.1f\t", tag, (unsigned)x.start, (unsigned)x.end,
                      x.meterStartWh, x.meterWh, x.estimatedWh, x.sunWh);
        char saved[40];
        std::snprintf(saved, sizeof saved, "\t%.2f\t%d\t%d", x.savedCents, x.mode, x.mixed ? 1 : 0);   // after the reason, so older readers still find it
        write(std::string(buffer) + x.reason + saved);
    };
    for (const auto& x : sessions_) if (since(x.end)) sessionLine('T', x);
    if (open_) sessionLine('O', *open_);
    for (const auto& e : events_) if (since(e.t)) write("E\t" + std::to_string(e.t) + "\t" + e.text);
    for (const auto& smp : samples_) {
        if (!since(smp.t)) continue;
        std::snprintf(buffer, sizeof buffer, "S\t%u\t%d\t%d\t%d\t%d\t%u\t%u", (unsigned)smp.t, smp.solar, smp.home, smp.grid,
                      smp.car, (unsigned)smp.mode, (unsigned)smp.flags);
        write(buffer);
    }
}

void HistoryService::forEachLongLine(const std::function<void(const std::string&)>& write, bool onlyNew) const {
    char buffer[96];
    auto emit = [&](char tag, const HistorySample& s) {
        std::snprintf(buffer, sizeof buffer, "%c\t%u\t%d\t%d\t%d\t%d\t%u\t%u", tag, (unsigned)s.t, s.solar, s.home, s.grid, s.car,
                      (unsigned)s.mode, (unsigned)s.flags);
        write(buffer);
    };
    size_t from5 = onlyNew && pendingFiveMin_ < fiveMin_.size() ? fiveMin_.size() - pendingFiveMin_ : 0;
    size_t fromH = onlyNew && pendingHourly_ < hourly_.size() ? hourly_.size() - pendingHourly_ : 0;
    for (size_t i = from5; i < fiveMin_.size(); i++) emit('M', fiveMin_.at(i));
    for (size_t i = fromH; i < hourly_.size(); i++) emit('H', hourly_.at(i));
}

void HistoryService::loadLine(const std::string& line) {
    std::vector<std::string> p = split(line, '\t');
    if (p.empty() || p[0].size() != 1) return;
    switch (p[0][0]) {
        case 'S':
            if (p.size() == 8) {
                HistorySample s;
                s.t = static_cast<uint32_t>(std::strtoul(p[1].c_str(), nullptr, 10));
                s.solar = static_cast<int16_t>(std::atoi(p[2].c_str()));
                s.home = static_cast<int16_t>(std::atoi(p[3].c_str()));
                s.grid = static_cast<int16_t>(std::atoi(p[4].c_str()));
                s.car = static_cast<int16_t>(std::atoi(p[5].c_str()));
                s.mode = static_cast<uint8_t>(std::atoi(p[6].c_str()));
                s.flags = static_cast<uint8_t>(std::atoi(p[7].c_str()));
                if (s.t == 0 || s.t <= lastSampleAt_) return;   // already loaded (a journal overlaps the full save)
                samples_.push_back(s);
                while (samples_.size() > kMaxSamples) samples_.pop_front();
                lastSampleAt_ = std::max(lastSampleAt_, s.t);
            }
            break;
        case 'M':
        case 'H':
            if (p.size() == 8) {
                HistorySample s;
                s.t = static_cast<uint32_t>(std::strtoul(p[1].c_str(), nullptr, 10));
                s.solar = static_cast<int16_t>(std::atoi(p[2].c_str()));
                s.home = static_cast<int16_t>(std::atoi(p[3].c_str()));
                s.grid = static_cast<int16_t>(std::atoi(p[4].c_str()));
                s.car = static_cast<int16_t>(std::atoi(p[5].c_str()));
                s.mode = static_cast<uint8_t>(std::atoi(p[6].c_str()));
                s.flags = static_cast<uint8_t>(std::atoi(p[7].c_str()));
                if (s.t == 0) return;
                (p[0][0] == 'M' ? fiveMin_ : hourly_).push(s);
            }
            break;
        case 'T':
        case 'O':
            if (p.size() >= 7) {
                HistorySession x;
                x.start = static_cast<uint32_t>(std::strtoul(p[1].c_str(), nullptr, 10));
                x.end = static_cast<uint32_t>(std::strtoul(p[2].c_str(), nullptr, 10));
                x.meterStartWh = std::atof(p[3].c_str());
                x.meterWh = std::atof(p[4].c_str());
                x.estimatedWh = std::atof(p[5].c_str());
                x.sunWh = std::atof(p[6].c_str());
                x.reason = p.size() > 7 ? p[7] : "";
                x.savedCents = p.size() > 8 ? std::atof(p[8].c_str()) : 0;   // older files have no money column
                if (p.size() > 10) { x.mode = std::atoi(p[9].c_str()); x.mixed = p[10] == "1"; }   // older files have no mode
                if (x.start == 0) return;
                if (p[0][0] == 'T') {
                    for (const auto& known : sessions_) if (known.start == x.start) return;
                    if (open_ && open_->start == x.start) open_.reset();
                    sessions_.push_back(x);
                    while (sessions_.size() > kMaxSessions) sessions_.pop_front();
                } else {
                    open_ = x;
                }
            }
            break;
        case 'E':
            if (p.size() >= 3) {
                // The text is everything after the second tab (it holds no tabs itself).
                uint32_t at = static_cast<uint32_t>(std::strtoul(p[1].c_str(), nullptr, 10));
                for (const auto& known : events_) if (known.t == at && known.text == p[2]) return;
                events_.push_back({static_cast<uint32_t>(std::strtoul(p[1].c_str(), nullptr, 10)), p[2]});
                while (events_.size() > kMaxEvents) events_.pop_front();
            }
            break;
        case 'D':
            if (p.size() == 7 || p.size() == 8) {
                DayTotals d;
                d.solarWh = std::atof(p[2].c_str());
                d.carWh = std::atof(p[3].c_str());
                d.carSolarWh = std::atof(p[4].c_str());
                d.importWh = std::atof(p[5].c_str());
                d.exportWh = std::atof(p[6].c_str());
                d.savedCents = p.size() == 8 ? std::atof(p[7].c_str()) : 0;   // older files have no money column
                days_[std::atoll(p[1].c_str())] = d;
            }
            break;
        default:
            break;
    }
}

double HistoryService::carEnergyWhSince(int64_t epochSeconds) const {
    double wh = 0;
    for (const HistorySample& s : samples_) {
        if (static_cast<int64_t>(s.t) >= epochSeconds) wh += s.car / 60.0;
    }
    return wh;
}

}  // namespace solar
