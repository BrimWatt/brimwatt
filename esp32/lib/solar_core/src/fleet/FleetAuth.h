#pragma once

#include <cstdint>
#include <string>

#include "common/Platform.h"
#include "fleet/FleetHttp.h"

namespace solar {
namespace fleet {

/**
 * What the board keeps to talk to the owner's Tesla account. NO client secret: refreshing a token needs only the client id and the
 * refresh token. The refresh token IS a secret (it opens the account's cars for the app's scopes): it lives in flash, is never printed,
 * logged or sent anywhere except to Tesla, and rotates on every use.
 */
struct FleetCredentials {
    std::string clientId;
    std::string refreshToken;
    std::string region = "na";   // "na" (Americas, Asia-Pacific without China) or "eu" (Europe, Middle East, Africa)
    bool complete() const { return !clientId.empty() && !refreshToken.empty(); }
};

/** Where the board keeps the signed-in account: flash on the board, a memory fake in the tests. */
class FleetCredentialStore {
public:
    virtual ~FleetCredentialStore() = default;
    virtual bool load(FleetCredentials& out) = 0;
    virtual void save(const FleetCredentials& in) = 0;
    virtual void clear() = 0;
};

/**
 * Keeps a fresh access token for the Fleet API. Tesla's refresh tokens are single use: every refresh returns a new one, which is saved
 * at once (before anything else can fail). Not thread safe: use it from one task at a time.
 */
class FleetAuth {
public:
    enum class State { NotConnected, Ok, NeedsSignIn, Offline };

    static constexpr const char* kTokenUrl = "https://fleet-auth.prd.vn.cloud.tesla.com/oauth2/v3/token";
    static constexpr int kRefreshMarginSeconds = 120;   // refresh this long before the token runs out
    static constexpr int kOfflineRetrySeconds = 30;
    static constexpr int kSignInRetrySeconds = 600;     // a refused token will not start working by itself: do not hammer Tesla

    FleetAuth(FleetHttp& http, FleetCredentialStore& store, Platform& platform) : http_(http), store_(store), platform_(platform) { reload(); }

    /** Re-reads the store (after a save from outside). */
    void reload();
    bool connected() const { return creds_.complete(); }
    State state() const { return state_; }
    const std::string& clientId() const { return creds_.clientId; }
    const std::string& region() const { return creds_.region; }
    /** Base URL of the regional Fleet API, no trailing slash. */
    std::string apiBase() const;
    /** Plain words for the last problem (empty when fine). Never contains a token. */
    const std::string& error() const { return error_; }

    /** Saves new credentials (the first sign-in, made on a computer) and forgets any token held. */
    void connect(const FleetCredentials& creds);
    /** Deletes the credentials. The app stays listed in the owner's Tesla account until they remove it there. */
    void forget();

    /** A valid access token, refreshed if needed; empty when there is none (see error() and state()). */
    std::string accessToken();
    /** Drops the held token so the next accessToken() asks Tesla for a new one (after a 401). */
    void invalidate() { access_.clear(); expiresAt_ = 0; }

private:
    bool refresh();
    uint64_t nowSeconds() const { return platform_.nowMillis() / 1000; }

    FleetHttp& http_;
    FleetCredentialStore& store_;
    Platform& platform_;
    FleetCredentials creds_;
    std::string access_;
    uint64_t expiresAt_ = 0;
    uint64_t retryAt_ = 0;
    State state_ = State::NotConnected;
    std::string error_;
};

}  // namespace fleet
}  // namespace solar
