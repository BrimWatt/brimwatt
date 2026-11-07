#pragma once

#include <string>

namespace solar {
namespace fleet {

/**
 * The owner's Tesla developer app, as the board needs it to sign an account in by itself. The client SECRET is on the board because Tesla's
 * sign-in code can only be exchanged with it. It is kept in flash, never printed, logged or served, and only ever sent to Tesla's own sign-in
 * server. Anyone holding the board could read it from flash: use an app of your own that you can reset in Tesla's developer dashboard.
 */
struct FleetApp {
    std::string clientId;
    std::string clientSecret;
    std::string domain;           // the public domain that hosts the app's public key and the sign-in callback page, e.g. wattsync.netlify.app
    std::string region = "na";
    bool ready() const { return !clientId.empty() && !clientSecret.empty() && !domain.empty(); }
};

class FleetAppStore {
public:
    virtual ~FleetAppStore() = default;
    virtual bool load(FleetApp& out) = 0;
    virtual void save(const FleetApp& in) = 0;
    virtual void clear() = 0;
};

}  // namespace fleet
}  // namespace solar
