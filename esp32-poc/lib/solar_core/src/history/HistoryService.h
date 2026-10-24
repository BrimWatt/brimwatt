#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "common/Platform.h"
#include "common/Ring.h"
#include "state/ChargingMode.h"

namespace solar {

/** Earlier than this (November 2023) means the clock has not been set yet: nothing is recorded. */
constexpr int64_t kMinValidEpoch = 1700000000;

/** One minute of power, in watts. */
struct HistorySample {
    uint32_t t = 0;         // epoch seconds
    int16_t solar = 0;
    int16_t home = 0;       // the house without the car
    int16_t grid = 0;       // positive importing, negative exporting
    int16_t car = 0;
    uint8_t mode = 0;       // 0 Eco, 1 Max solar, 2 Custom
    uint8_t flags = 0;      // bit 0: the car was charging, bit 1: charging was paused
};

constexpr uint8_t kFlagCharging = 1;
constexpr uint8_t kFlagPaused = 2;

/** Energy for one local day. */
struct DayTotals {
    double solarWh = 0;       // produced
    double carWh = 0;         // into the car
    double carSolarWh = 0;    // the part of that which the sun covered
    double importWh = 0;
    double exportWh = 0;
    double savedCents = 0;    // the sun's share of the car's energy valued at the price of each hour (0 if not recorded)
};

/**
 * One charging session, from the wallbox's StartTransaction to its StopTransaction. The energy is the wallbox's own
 * meter difference when it reported both readings, otherwise our estimate; the part from the sun is always our
 * estimate (the solar left after the house, up to what the car drew).
 */
struct HistorySession {
    uint32_t start = 0;
    uint32_t end = 0;           // 0 while the session is still running
    double meterStartWh = -1;   // the wallbox's meter at the start, or -1 if it did not say
    double meterWh = -1;        // the wallbox's measured energy for the session, or -1 if unknown
    double estimatedWh = 0;     // our integration of what the car drew
    double sunWh = 0;           // the part of that which the sun covered
    std::string reason;         // why it ended, as the wallbox reported it (Remote, EVDisconnected, ...)
    int mode = -1;              // mode code (0 Eco, 1 Max solar, 2 Custom) at the first reading of the session, -1 if unknown
    bool mixed = false;         // the mode changed during the session
    double savedCents = 0;      // what the sun's share was worth at the prices of the hours it was charged

    double energyWh() const { return meterWh >= 0 ? meterWh : estimatedWh; }
};

struct HistoryEvent {
    uint32_t t = 0;
    std::string text;
};

/**
 * What happened and how much power flowed, kept for the dashboard's graphs, at lower detail the older it gets:
 * a sample every minute for 2 days, every 5 minutes for 30 days and every hour for a year, plus daily energy totals
 * for a year (from which the savings are worked out) and a short activity log. The whole year takes about 0.4 MB.
 *
 * Energy is integrated from the once-a-minute readings, so it is an estimate, typically within a few percent.
 * The part of the car's energy that came from the sun is whatever solar was left after the house, up to what
 * the car drew.
 */
class HistoryService {
public:
    static constexpr size_t kMaxSamples = 2880;   // 48 hours at one a minute
    static constexpr size_t kMaxFiveMinute = 8640; // 30 days at one every 5 minutes
    static constexpr size_t kMaxHourly = 8760;     // a year at one an hour
    static constexpr size_t kMaxEvents = 50;
    static constexpr size_t kMaxDays = 400;
    static constexpr size_t kMaxSessions = 400;

    explicit HistoryService(const Platform& platform) : platform_(platform), fiveMin_(kMaxFiveMinute), hourly_(kMaxHourly) {}

    /** Where the price of electricity at a moment (cents per kWh at an epoch second) comes from, for the money saved. */
    void setPriceProvider(std::function<double(int64_t)> priceAt) { priceAt_ = std::move(priceAt); }

    /** Records the latest readings. `carWatts` is what the charger is drawing (measured, or assumed from its limit). */
    void recordSample(double solarWatts, double gridWatts, double carWatts, ChargingMode mode, bool paused);

    /** The wallbox started a session; `meterStartWh` is its meter reading, or -1 if it gave none. */
    void sessionStarted(double meterStartWh);

    /** The wallbox ended the session. Ignored if we never saw it start (the app was not running). */
    void sessionStopped(double meterStopWh, const std::string& reason);

    /** The session running now, if any. */
    const HistorySession* openSession() const { return open_ ? &*open_ : nullptr; }

    /** Finished sessions, newest first. */
    std::vector<HistorySession> recentSessions(size_t limit, size_t offset = 0) const;
    size_t sessionCount() const { return sessions_.size(); }

    /** Adds a line to the activity log. The same text within two minutes is ignored. */
    void addEvent(const std::string& text);

    /** Samples from `from` up to now, averaged into buckets of `stepSeconds` (a step of 0 or 60 returns them as they are). */
    std::vector<HistorySample> series(int64_t from, int stepSeconds) const;

    /**
     * Samples between `from` and `to`, taken from the finest data that still exists for each moment (minutes, then 5
     * minutes, then hours), averaged into buckets of `stepSeconds`.
     */
    std::vector<HistorySample> seriesRange(int64_t from, int64_t to, int stepSeconds) const;

    DayTotals totalsForDay(int64_t epochDay) const;

    /** Every day with data, oldest first, as (local day number, totals). */
    const std::map<int64_t, DayTotals>& allDays() const { return days_; }

    /** How many one-minute samples are held (the finest tier). */
    /** Told about every new activity line (not the repeats that are dropped); must not block. */
    void setEventListener(std::function<void(const std::string&)> listener) { eventListener_ = std::move(listener); }

    size_t minuteSampleCount() const { return samples_.size(); }

    /** Energy that went into the car since an epoch second, from the minute-by-minute samples. */
    double carEnergyWhSince(int64_t epochSeconds) const;

    /** Newest first. */
    std::vector<HistoryEvent> recentEvents(size_t limit, size_t offset = 0) const;
    size_t eventCount() const { return events_.size(); }

    /** Midnight (epoch seconds) at the start of the local day containing `epoch`. */
    int64_t startOfLocalDay(int64_t epoch) const;
    int64_t localDay(int64_t epoch) const;

    // ---- saving ----

    /** Calls `write` once per line of the saved form. */
    void forEachLine(const std::function<void(const std::string&)>& write) const;
    /** Reads back one line written by forEachLine. Unreadable lines are ignored. */
    void loadLine(const std::string& line);
    /**
     * The 5-minute and hourly records, kept in a file of their own that only ever grows at the end (so saving them is
     * cheap). `onlyNew` writes just the records added since the last markLongSaved().
     */
    void forEachLongLine(const std::function<void(const std::string&)>& write, bool onlyNew) const;
    bool longPending() const { return pendingFiveMin_ > 0 || pendingHourly_ > 0; }
    void markLongSaved() { pendingFiveMin_ = pendingHourly_ = 0; }
    /** How many records a full rewrite of the long file would hold. */
    size_t longRecordCount() const { return fiveMin_.size() + hourly_.size(); }

    /** True if something changed since the last markClean(). */
    bool dirty() const { return dirty_; }
    void markClean();

    /**
     * A small file written every minute between the full saves: the samples, events, sessions and today's totals
     * added since the last full save. After a power cut it is replayed on top of the full save (loadLine ignores
     * what is already there), so at most a minute is lost. A full save makes it obsolete.
     */
    void forEachJournalLine(const std::function<void(const std::string&)>& write) const;
    bool journalPending() const { return journalDirty_; }
    void markJournalSaved() { journalDirty_ = false; }

private:
    struct Accum {
        double solar = 0, home = 0, grid = 0, car = 0;
        int n = 0;
        uint8_t flags = 0, mode = 0;
        int64_t bucket = -1;
    };

    void feedFiveMinute(const HistorySample& sample);
    void flushFiveMinute();
    void feedHourly(const HistorySample& fiveMinute);
    void flushHourly();
    static HistorySample average(const Accum& a, int64_t middle);
    static std::vector<HistorySample> bucketed(const std::vector<HistorySample>& raw, int64_t from, int stepSeconds);

    const Platform& platform_;
    std::function<void(const std::string&)> eventListener_;
    std::deque<HistorySample> samples_;
    Ring<HistorySample> fiveMin_;
    Ring<HistorySample> hourly_;
    Accum acc5_, acc60_;
    size_t pendingFiveMin_ = 0, pendingHourly_ = 0;
    std::deque<HistoryEvent> events_;
    std::map<int64_t, DayTotals> days_;
    std::deque<HistorySession> sessions_;
    std::optional<HistorySession> open_;
    uint32_t lastSampleAt_ = 0;
    bool dirty_ = false;
    bool journalDirty_ = false;
    std::function<double(int64_t)> priceAt_ = [](int64_t) { return 30.0; };
    int64_t mainSavedAt_ = 0;
};

}  // namespace solar
