#pragma once

// The setup network's logic, with no hardware in it: what the setup form may contain, how a Wi-Fi change is tried
// before it is kept, and how the scan list is tidied. The firmware supplies the radio and the flash (src/net, src/platform).

#include <cstddef>
#include <string>
#include <vector>

namespace solar::setup {

// The temporary network the board opens for setup. The password is fixed for now (8 characters is the WPA2 minimum).
constexpr const char* kApName = "WattSync-Setup";
constexpr const char* kApPassword = "wattsync";
constexpr const char* kPortalAddress = "192.168.4.1";

constexpr size_t kMaxSsidBytes = 32;
constexpr size_t kMinWifiPassword = 8;
constexpr size_t kMaxWifiPassword = 63;
constexpr size_t kMinToken = 20;
constexpr size_t kMaxToken = 4096;
constexpr size_t kMinUpdatePassword = 8;
constexpr size_t kMaxUpdatePassword = 64;

/** What the board needs to know about its surroundings. Empty fields mean "not set". */
struct Credentials {
    std::string wifiSsid;
    std::string wifiPassword;
    std::string enphaseToken;
    std::string envoyIp;       // saved as "auto" when the user left it empty on purpose; see effective()
    std::string otaPassword;   // the password for updating the board over Wi-Fi

    bool hasWifi() const { return !wifiSsid.empty(); }
};

/** A Wi-Fi change is tried on the next start; if the board cannot join, the old network is put back. */
struct TrialState {
    bool pending = false;
    std::string previousSsid;
    std::string previousPassword;
    std::string lastError;   // shown on the setup page; empty when nothing went wrong
};

/** Where the values are kept: the firmware uses flash (NVS), tests use memory. */
class CredentialStore {
public:
    virtual ~CredentialStore() = default;
    virtual Credentials load() = 0;   // only what was saved from the setup page
    virtual void save(const Credentials& credentials) = 0;
    virtual TrialState loadTrial() = 0;
    virtual void saveTrial(const TrialState& trial) = 0;
    virtual void clear() = 0;         // forget everything saved from the setup page
};

/**
 * The values to use: what was saved, field by field, else the compiled-in default (secrets.h). The Envoy address
 * "auto" becomes empty, meaning "find it as envoy.local".
 */
Credentials effective(const Credentials& saved, const Credentials& compiledDefaults);

/** What the setup form sent. Empty secret fields mean "keep what is saved". */
struct Form {
    std::string ssid;
    std::string password;
    bool open = false;   // the chosen network has no password
    std::string token;
    std::string envoyIp;   // empty = find the gateway automatically
    std::string updatePassword;
};

struct Validation {
    std::string error;       // empty when the form is fine; otherwise a sentence for the user
    Credentials merged;      // what to save (everything, including the kept values)
    bool ok() const { return error.empty(); }
};

/** `current` is what is in use now (see effective()). */
Validation validate(const Form& form, const Credentials& current);

/** Strips spaces, line breaks, surrounding quotes and a leading "Bearer ", which pasted tokens often carry. */
std::string cleanToken(const std::string& pasted);

/** Dotted decimal, four numbers from 0 to 255, no leading zeros, not 0.0.0.0. */
bool isValidIpv4(const std::string& text);

struct ScanEntry {
    std::string ssid;
    int rssi = -100;
    bool secure = true;
};

/** Hidden networks (no name) dropped, repeats of one name merged into the strongest, strongest first. */
std::vector<ScanEntry> tidyScan(std::vector<ScanEntry> raw);

struct TrialOutcome {
    bool wasTrial = false;
    bool joined = false;
};

/**
 * Call after the start-up attempt to join Wi-Fi. If a trial was pending: success keeps the new network; failure
 * puts the previous one back and remembers why, for the setup page.
 */
TrialOutcome finishTrial(CredentialStore& store, bool joined, const std::string& reason);

std::string joinFailureMessage(const std::string& ssid, const std::string& reason, bool previousKept);

}  // namespace solar::setup
