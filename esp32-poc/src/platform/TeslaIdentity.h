#pragma once

#include "fleet/FleetCar.h"
#include "platform/NvsTeslaStore.h"

namespace device {

/** The VIN and signing key the Bluetooth side already keeps, offered to the Fleet link (the same car, the same key). */
class TeslaIdentity : public solar::fleet::FleetIdentity {
public:
    explicit TeslaIdentity(NvsTeslaStore& store) : store_(store) {}
    std::string vin() override { return store_.loadVin(); }
    bool privateKey(solar::tesla::Bytes& out) override { return store_.loadKey(out); }
    void saveVin(const std::string& vin) override { store_.saveVin(vin); }
    void saveKey(const solar::tesla::Bytes& privateKey) override { store_.saveKey(privateKey); }

private:
    NvsTeslaStore& store_;
};

}  // namespace device
