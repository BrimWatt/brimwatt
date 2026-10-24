#include "state/ChargerPersistence.h"

#include "common/Config.h"
#include "common/Log.h"

namespace solar {

void ChargerPersistence::restore() {
    known_ = store_.load();
    LOG_INFO("Restored %u known charger(s) from flash.", (unsigned)known_.size());
}

std::optional<ChargerSnapshot> ChargerPersistence::recall(const std::string& chargerId) const {
    auto it = known_.find(chargerId);
    if (it == known_.end()) return std::nullopt;
    int64_t ageSeconds = platform_.epochSeconds() - it->second.savedAtSeconds;
    if (ageSeconds > config::kStateMaxAgeSeconds) return std::nullopt;
    return it->second;
}

void ChargerPersistence::onDisconnect(Charger& charger) {
    known_[charger.id()] = charger.snapshot(platform_.epochSeconds());
    dirty_ = true;
    save(false);
}

void ChargerPersistence::tick() {
    uint64_t now = platform_.nowMillis();
    if (now - lastSaveMillis_ < config::kStateSaveIntervalMillis) return;
    lastSaveMillis_ = now;
    save(false);
}

void ChargerPersistence::save(bool refreshTimestamps) {
    int64_t now = platform_.epochSeconds();
    chargers_.forEach([&](int, Charger& charger) { remember(charger.id(), charger, now, refreshTimestamps); });
    if (!dirty_) return;
    store_.save(known_);
    dirty_ = false;
}

void ChargerPersistence::remember(const std::string& id, Charger& charger, int64_t nowSeconds, bool refreshTimestamps) {
    ChargerSnapshot current = charger.snapshot(nowSeconds);
    auto previous = known_.find(id);
    bool changed = previous == known_.end() || !current.sameStateAs(previous->second);
    bool stale = previous != known_.end() && nowSeconds - previous->second.savedAtSeconds >= config::kStateRefreshSeconds;
    if (changed || stale || refreshTimestamps) {
        known_[id] = current;
        dirty_ = true;
    }
}

}  // namespace solar
