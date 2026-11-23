#pragma once

#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

#include "common/Config.h"

namespace solar {

/** Where the solar panels are: for sunrise and sunset, the clock and the weather forecast. */
struct Site {
    double latitude = config::kSiteLatitude;
    double longitude = config::kSiteLongitude;
    std::string timezone = config::kSiteTimezone;   // the IANA name, e.g. America/Los_Angeles
    std::string name;                               // for display, e.g. "Lathrop, California" (may be empty)
};

/** Where the site is kept. Changing it restarts the board so the clock and the daylight window start from it. */
class SiteControl {
public:
    virtual ~SiteControl() = default;
    virtual Site current() const = 0;          // what the board is using now (the saved site, or the built-in default)
    virtual bool isSet() const = 0;            // the owner has saved one
    virtual bool restartPending() const = 0;
    virtual void save(const Site& site) = 0;   // keeps it and restarts the board soon
};

namespace timezones {

/**
 * The rule the board's clock needs for an IANA zone name (a POSIX TZ string, daylight saving included), or nothing if the board does not
 * know the zone yet. The United States and the Canadian zones that follow the same rules. Add more here as they are checked.
 */
inline std::optional<std::string> posixFor(const std::string& iana) {
    static const char* kPacific = "PST8PDT,M3.2.0,M11.1.0";
    static const char* kMountain = "MST7MDT,M3.2.0,M11.1.0";
    static const char* kCentral = "CST6CDT,M3.2.0,M11.1.0";
    static const char* kEastern = "EST5EDT,M3.2.0,M11.1.0";
    static const char* kAlaska = "AKST9AKDT,M3.2.0,M11.1.0";
    static const char* kAtlantic = "AST4ADT,M3.2.0,M11.1.0";
    struct Row { const char* name; const char* rule; };
    static const Row rows[] = {
        {"America/Los_Angeles", kPacific}, {"America/Vancouver", kPacific}, {"America/Tijuana", kPacific},
        {"America/Denver", kMountain}, {"America/Boise", kMountain}, {"America/Edmonton", kMountain},
        {"America/Phoenix", "MST7"},
        {"America/Chicago", kCentral}, {"America/Winnipeg", kCentral}, {"America/Menominee", kCentral}, {"America/Indiana/Knox", kCentral},
        {"America/Indiana/Tell_City", kCentral}, {"America/North_Dakota/Center", kCentral}, {"America/North_Dakota/New_Salem", kCentral},
        {"America/North_Dakota/Beulah", kCentral},
        {"America/New_York", kEastern}, {"America/Toronto", kEastern}, {"America/Detroit", kEastern}, {"America/Indiana/Indianapolis", kEastern},
        {"America/Indiana/Marengo", kEastern}, {"America/Indiana/Petersburg", kEastern}, {"America/Indiana/Vevay", kEastern},
        {"America/Indiana/Vincennes", kEastern}, {"America/Indiana/Winamac", kEastern}, {"America/Kentucky/Louisville", kEastern},
        {"America/Kentucky/Monticello", kEastern},
        {"America/Anchorage", kAlaska}, {"America/Juneau", kAlaska}, {"America/Sitka", kAlaska}, {"America/Nome", kAlaska},
        {"America/Yakutat", kAlaska}, {"America/Metlakatla", kAlaska},
        {"Pacific/Honolulu", "HST10"},
        {"America/Halifax", kAtlantic}, {"America/Puerto_Rico", "AST4"},
    };
    for (const Row& r : rows) if (iana == r.name) return std::string(r.rule);
    return std::nullopt;
}

}  // namespace timezones

/** A whole decimal number ("37.84", "-121.3"); nothing for anything else (text after the number, empty, not finite). */
inline std::optional<double> parseDecimal(const std::string& text) {
    if (text.empty()) return std::nullopt;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(value)) return std::nullopt;
    return value;
}

/**
 * Checks what the dashboard sent and makes the Site to keep: latitude and longitude to two decimals (about a kilometre), a time zone the
 * board knows, and a display name of up to 60 printable characters. On failure returns nothing and sets `error` to the plain message
 * (the same words the Pi app uses).
 */
inline std::optional<Site> parseSite(const std::string& lat, const std::string& lon, const std::string& tz, const std::string& name, std::string& error) {
    auto la = parseDecimal(lat);
    if (!la || *la < -90 || *la > 90) { error = "Latitude must be a number from -90 to 90."; return std::nullopt; }
    auto lo = parseDecimal(lon);
    if (!lo || *lo < -180 || *lo > 180) { error = "Longitude must be a number from -180 to 180."; return std::nullopt; }
    if (!timezones::posixFor(tz)) { error = "That time zone is not supported on this controller yet."; return std::nullopt; }
    if (name.size() > 60) { error = "The place name must be 60 characters or fewer."; return std::nullopt; }
    for (unsigned char c : name) if (c < 32 || c == 127) { error = "The place name has a character that is not allowed."; return std::nullopt; }
    Site site;
    site.latitude = std::round(*la * 100) / 100;
    site.longitude = std::round(*lo * 100) / 100;
    site.timezone = tz;
    site.name = name;
    return site;
}

}  // namespace solar
