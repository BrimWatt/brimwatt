#pragma once

#include "fleet/FleetAuth.h"
#include "tesla/Proto.h"

namespace device {

/**
 * The Tesla account link, in the flash's own "fleet" area: the app's client id, the region and the REFRESH TOKEN (a secret: it opens the
 * owner's cars for the app's permissions). The token is never printed, logged or served; the flash is not encrypted, so anyone holding the
 * board could read it. The owner can cancel it any time in their Tesla account (Security > Third-party apps).
 */
class NvsFleetStore : public solar::fleet::FleetCredentialStore {
public:
    bool load(solar::fleet::FleetCredentials& out) override;
    void save(const solar::fleet::FleetCredentials& in) override;
    void clear() override;

    /** The app's signing key (32 bytes), kept apart from the Bluetooth key so that connecting an account never replaces a Bluetooth pairing. */
    bool loadKey(solar::tesla::Bytes& out);
    void saveKey(const solar::tesla::Bytes& key);
};

}  // namespace device
