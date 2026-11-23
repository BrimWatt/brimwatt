#pragma once

#include <mutex>
#include <string>

#include "common/Platform.h"
#include "fleet/FleetApi.h"
#include "fleet/FleetApp.h"
#include "fleet/FleetAuth.h"
#include "fleet/FleetCar.h"
#include "tesla/TeslaCrypto.h"

namespace solar {
namespace fleet {

/** What the dashboard may know about the Tesla account link. Never a token, a secret or a full id. */
struct FleetStatus {
    bool supported = false;       // false where there is no Fleet link (the Pi)
    bool connected = false;       // an account is signed in (a refresh token is saved)
    std::string state;            // "not_connected", "ok", "needs_sign_in", "offline"
    std::string error;            // plain words, empty when fine
    std::string region;
    std::string client;           // masked client id, e.g. "ab12...f9"
    bool hasVin = false;
    bool hasKey = false;
    bool appReady = false;        // the developer app (client id, secret, domain) is on the board, so the board can sign an account in by itself
    std::string domain;           // public: the app's domain (for the car's key link)
    std::string signIn;           // "idle", "waiting", "working", "done", "failed"
    std::string signInText;       // plain words about the last sign-in
};

/** What the API needs for the account link. */
class FleetControl {
public:
    virtual ~FleetControl() = default;
    virtual FleetStatus status() const = 0;
    /**
     * Saves an account that a computer signed in (the tool): client id, refresh token, region. `vin` and `keyHex` (the signing key, 64 hex
     * digits) are optional. Returns an error text, or empty when saved. Secrets arrive in a POST body, are never echoed and never logged.
     */
    virtual std::string connect(const FleetCredentials& creds, const std::string& vin, const std::string& keyHex) = 0;
    virtual void disconnect() = 0;

    /** The developer app the board signs accounts in with, and (optionally) the signing key. Returns an error text, or empty when saved. */
    virtual std::string saveApp(const FleetApp& app, const std::string& keyHex) = 0;
    /**
     * Starts a sign-in on the board: `origin` is the address the browser used to reach the board (so Tesla's answer can find its way back).
     * Returns an error text, or empty with `url` set to the Tesla sign-in page to open.
     */
    virtual std::string beginSignIn(const std::string& origin, std::string& url) = 0;
    /** Tesla's answer, forwarded by the callback page: checks the state, exchanges the code in the background. Error text or empty. */
    virtual std::string completeSignIn(const std::string& code, const std::string& state) = 0;
    /** Chooses the car (when the account has none or several). Error text or empty. */
    virtual std::string setVin(const std::string& vin) = 0;
};

/** The portable implementation: validates, then saves through FleetAuth, the app store and the identity store; runs the sign-in. */
class FleetManager : public FleetControl {
public:
    static constexpr const char* kAuthorizeUrl = "https://auth.tesla.com/oauth2/v3/authorize";
    static constexpr const char* kCallbackPage = "/callback.html";
    static constexpr const char* kScopes = "openid offline_access vehicle_device_data vehicle_cmds vehicle_charging_cmds";
    static constexpr int kSignInWindowSeconds = 600;

    FleetManager(FleetAuth& auth, FleetApi& api, FleetIdentity& identity, FleetAppStore& apps, FleetHttp& http, tesla::Crypto& crypto,
                 Platform& platform, FleetExecutor& executor)
        : auth_(auth), api_(api), identity_(identity), apps_(apps), http_(http), crypto_(crypto), platform_(platform), executor_(executor) {}

    FleetStatus status() const override;
    std::string connect(const FleetCredentials& creds, const std::string& vin, const std::string& keyHex) override;
    void disconnect() override { auth_.forget(); }
    std::string saveApp(const FleetApp& app, const std::string& keyHex) override;
    std::string beginSignIn(const std::string& origin, std::string& url) override;
    std::string completeSignIn(const std::string& code, const std::string& state) override;
    std::string setVin(const std::string& vin) override;

    /** Letters and digits of a pasted VIN, capitals; spaces and dashes dropped. */
    static std::string cleanVin(const std::string& raw);
    static bool parseKeyHex(const std::string& hex, tesla::Bytes& out);
    /** Only a board on the home network may be sent the sign-in code: an address like http://192.168.1.50, http://10.0.0.5:80, http://wattsync.local or http://localhost. */
    static bool isLocalOrigin(const std::string& origin);

private:
    void exchange(const FleetApp& app, const std::string& code);
    void finish(bool ok, const std::string& text);

    FleetAuth& auth_;
    FleetApi& api_;
    FleetIdentity& identity_;
    FleetAppStore& apps_;
    FleetHttp& http_;
    tesla::Crypto& crypto_;
    Platform& platform_;
    FleetExecutor& executor_;

    mutable std::mutex m_;
    std::string nonce_;           // empty when no sign-in is waiting
    std::string origin_;
    uint64_t expiresAtMs_ = 0;
    std::string signIn_ = "idle";
    std::string signInText_;
};

}  // namespace fleet
}  // namespace solar
