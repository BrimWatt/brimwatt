#pragma once

#include "fleet/FleetCar.h"
#include "platform/NvsFleetStore.h"
#include "platform/NvsTeslaStore.h"

namespace device {

/**
 * The car and the key that signs for it, as the Fleet link sees them. The VIN is shared with the Bluetooth side (one board, one car). The key
 * is the app's own key when one was handed over at sign-in (it is the key the car's owner approves through Tesla's link), otherwise the board's
 * Bluetooth key. Handing over an app key never replaces the Bluetooth key; disconnecting the account removes the app key again.
 */
class TeslaIdentity : public solar::fleet::FleetIdentity {
public:
    TeslaIdentity(NvsTeslaStore& tesla, NvsFleetStore& fleet) : tesla_(tesla), fleet_(fleet) {}
    std::string vin() override { return tesla_.loadVin(); }
    bool privateKey(solar::tesla::Bytes& out) override { return fleet_.loadKey(out) || tesla_.loadKey(out); }
    void saveVin(const std::string& vin) override { tesla_.saveVin(vin); }
    void saveKey(const solar::tesla::Bytes& privateKey) override { fleet_.saveKey(privateKey); }

private:
    NvsTeslaStore& tesla_;
    NvsFleetStore& fleet_;
};

}  // namespace device
