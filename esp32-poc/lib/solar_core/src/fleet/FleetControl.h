#pragma once

#include <string>

#include "fleet/FleetAuth.h"
#include "fleet/FleetCar.h"
#include "tesla/TeslaCrypto.h"

namespace solar {
namespace fleet {

/** What the dashboard may know about the Tesla account link. Never a token, a secret or a full id. */
struct FleetStatus {
    bool supported = false;       // false where there is no Fleet link (the Pi)
    bool connected = false;       // credentials are saved
    std::string state;            // "not_connected", "ok", "needs_sign_in", "offline"
    std::string error;            // plain words, empty when fine
    std::string region;
    std::string client;           // masked client id, e.g. "ab12...f9"
    bool hasVin = false;
    bool hasKey = false;
};

/** What the API needs for the account link: look, save what a computer collected at sign-in, and delete. */
class FleetControl {
public:
    virtual ~FleetControl() = default;
    virtual FleetStatus status() const = 0;
    /**
     * Saves the account link. `vin` and `keyHex` (the signing key, 64 hex digits) are optional when the board already has them (from
     * Bluetooth pairing). Returns an error text, or empty when saved. Secrets arrive in a POST body, are never echoed and never logged.
     */
    virtual std::string connect(const FleetCredentials& creds, const std::string& vin, const std::string& keyHex) = 0;
    virtual void disconnect() = 0;
};

/** The portable implementation: validates, then saves through FleetAuth and the identity store. */
class FleetManager : public FleetControl {
public:
    FleetManager(FleetAuth& auth, FleetIdentity& identity, tesla::Crypto& crypto) : auth_(auth), identity_(identity), crypto_(crypto) {}

    FleetStatus status() const override;
    std::string connect(const FleetCredentials& creds, const std::string& vin, const std::string& keyHex) override;
    void disconnect() override { auth_.forget(); }

    /** Letters and digits of a pasted VIN, capitals; spaces and dashes dropped. */
    static std::string cleanVin(const std::string& raw);
    static bool parseKeyHex(const std::string& hex, tesla::Bytes& out);

private:
    FleetAuth& auth_;
    FleetIdentity& identity_;
    tesla::Crypto& crypto_;
};

}  // namespace fleet
}  // namespace solar
