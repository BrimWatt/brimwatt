#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "wallbox/Charger.h"

namespace solar {

/** The chargers currently connected, keyed by the connection they arrived on. */
class ChargerRegistry {
public:
    /**
     * Adds a charger. If one with the same id is already registered (the wallbox reconnected before we noticed
     * the old connection die), the old one is dropped and its key is returned so the caller can close it.
     */
    std::optional<int> add(int key, const std::string& id, ChargerLink& link, uint64_t nowMillis) {
        std::optional<int> replaced;
        for (auto it = chargers_.begin(); it != chargers_.end(); ++it) {
            if (it->second->id() == id) {
                replaced = it->first;
                chargers_.erase(it);
                break;
            }
        }
        chargers_[key] = std::make_unique<Charger>(id, link, nowMillis);
        return replaced;
    }

    Charger* find(int key) {
        auto it = chargers_.find(key);
        return it == chargers_.end() ? nullptr : it->second.get();
    }

    void remove(int key) { chargers_.erase(key); }

    void forEach(const std::function<void(int key, Charger&)>& fn) {
        for (auto& entry : chargers_) fn(entry.first, *entry.second);
    }

    /** Keys of chargers that have not sent anything for longer than the timeout. */
    std::vector<int> silentFor(uint64_t nowMillis, uint64_t timeoutMillis) const {
        std::vector<int> keys;
        for (const auto& entry : chargers_) {
            if (nowMillis - entry.second->lastSeenMillis() > timeoutMillis) keys.push_back(entry.first);
        }
        return keys;
    }

    size_t size() const { return chargers_.size(); }
    bool empty() const { return chargers_.empty(); }

private:
    std::map<int, std::unique_ptr<Charger>> chargers_;
};

}  // namespace solar
