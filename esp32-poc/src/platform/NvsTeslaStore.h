#pragma once

#include <string>

#include "car/CarSettings.h"
#include "tesla/Proto.h"

namespace device {

/**
 * What the controller keeps about its Tesla, in the flash's own "tesla" area: the private key it made for itself (32 bytes) and the
 * car's VIN. Both are needed to sign commands, so both stay on the board. Neither is ever printed, logged or sent anywhere; the flash
 * is not encrypted, so anyone holding the board could read them. The key only works for a car that was paired with it, and the owner can
 * delete it from the car at any time (Controls > Locks).
 */
class NvsTeslaStore : public solar::CarSettingsStore {
public:
    bool loadKey(solar::tesla::Bytes& privateKey);
    void saveKey(const solar::tesla::Bytes& privateKey);
    std::string loadVin();
    void saveVin(const std::string& vin);
    /** Whether the controller reads the car now and then (read only). On unless switched off. */
    bool loadWatch();
    void saveWatch(bool on);
    // who is in command and what to do when the car is back after an outage (CarSettingsStore)
    bool loadThrough() override;
    void saveThrough(bool car) override;
    int loadOnReturn() override;
    void saveOnReturn(int value) override;
    /** Forgets the key and the VIN. */
    void clear();
};

}  // namespace device
