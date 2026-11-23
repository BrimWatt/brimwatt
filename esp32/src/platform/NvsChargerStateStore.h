#pragma once

#include "state/ChargerPersistence.h"

namespace device {

/** Keeps the charger snapshots in the flash's non-volatile storage, as one small JSON text. */
class NvsChargerStateStore : public solar::ChargerStateStore {
public:
    std::map<std::string, solar::ChargerSnapshot> load() override;
    void save(const std::map<std::string, solar::ChargerSnapshot>& snapshots) override;
};

}  // namespace device
