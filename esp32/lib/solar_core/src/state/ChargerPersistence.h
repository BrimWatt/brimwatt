#pragma once

#include <map>
#include <optional>
#include <string>

#include "common/Platform.h"
#include "wallbox/ChargerRegistry.h"

namespace solar {

/** Where charger snapshots are kept. The firmware uses the flash's non-volatile storage; tests use memory. */
class ChargerStateStore {
public:
    virtual ~ChargerStateStore() = default;
    virtual std::map<std::string, ChargerSnapshot> load() = 0;
    virtual void save(const std::map<std::string, ChargerSnapshot>& snapshots) = 0;
};

/**
 * Keeps the last known state of each charger, so a restart of the board (or a dropped connection) does not lose
 * track of a charge in progress: the status, the transaction id and the limit we set.
 *
 * Writes happen only when something changed, at most every few seconds, plus a timestamp refresh every few
 * minutes, to keep wear on the flash low. Same behaviour as the Java app's StatePersistence.
 */
class ChargerPersistence {
public:
    ChargerPersistence(const Platform& platform, ChargerRegistry& chargers, ChargerStateStore& store)
        : platform_(platform), chargers_(chargers), store_(store) {}

    /** Loads the saved snapshots. Call once at start-up. */
    void restore();

    /** The last known state of a charger, if it is recent enough to be worth trusting. */
    std::optional<ChargerSnapshot> recall(const std::string& chargerId) const;

    /** Captures a charger's state the moment its connection goes away, before the next save can miss it. */
    void onDisconnect(Charger& charger);

    /** Call often from the main loop; saves at most every few seconds, and only if something changed. */
    void tick();

private:
    void save(bool refreshTimestamps);
    void remember(const std::string& id, Charger& charger, int64_t nowSeconds, bool refreshTimestamps);

    const Platform& platform_;
    ChargerRegistry& chargers_;
    ChargerStateStore& store_;
    std::map<std::string, ChargerSnapshot> known_;
    bool dirty_ = false;
    uint64_t lastSaveMillis_ = 0;
};

}  // namespace solar
