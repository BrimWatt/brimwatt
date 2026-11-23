#pragma once

#include "state/SettingsStore.h"

namespace device {

/** Keeps the settings in the flash's non-volatile storage, so they survive a restart or power cut. */
class NvsSettingsStore : public solar::SettingsStore {
public:
    std::optional<solar::Settings> load() override;
    void save(const solar::Settings& settings) override;
};

}  // namespace device
