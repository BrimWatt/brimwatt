#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "setup/SetupLogic.h"
#include "web/ApiHandler.h"

namespace solar::setup {

/** What the setup API needs from the board: the radio, the clock and the restart. */
class SetupHost {
public:
    virtual ~SetupHost() = default;
    virtual bool active() const = 0;                       // is the setup network open?
    virtual bool scanning() const = 0;
    virtual std::vector<ScanEntry> networks() const = 0;   // the latest scan, already tidied
    virtual void startScan() = 0;
    virtual uint32_t secondsLeft() const = 0;              // until the setup network closes; 0 = no limit
    virtual void userActive() = 0;                         // somebody is using the page: keep it open
    virtual void restartSoon() = 0;                        // after the reply has been sent
};

/**
 * The setup page's endpoints, independent of the web server:
 *
 *   GET  /api/setup/status     which settings are saved (never their secret values), the last Wi-Fi error, time left
 *   GET  /api/setup/networks   the networks the board can see, strongest first
 *   POST /api/setup/scan       look again
 *   POST /api/setup/save       ssid, password, open, token, envoyIp, updatePassword (empty secrets keep what is saved)
 *
 * Everything answers 403 while the setup network is closed. Secrets are write-only: no reply contains one.
 */
class SetupApi {
public:
    SetupApi(CredentialStore& store, SetupHost& host, Credentials compiledDefaults)
        : store_(store), host_(host), defaults_(std::move(compiledDefaults)) {}

    static bool owns(const std::string& path) { return path.rfind("/api/setup/", 0) == 0; }

    HttpResponse handle(const std::string& method, const std::string& path, const ApiHandler::Params& params,
                        bool hasSafeHeader);

private:
    HttpResponse status();
    HttpResponse networks();
    HttpResponse save(const ApiHandler::Params& params);
    Credentials current() { return effective(store_.load(), defaults_); }

    CredentialStore& store_;
    SetupHost& host_;
    Credentials defaults_;
};

}  // namespace solar::setup
