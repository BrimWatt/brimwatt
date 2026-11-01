#pragma once

#include <Arduino.h>

#include <functional>

#include "site/Site.h"

namespace device {

/**
 * Where the panels are (latitude, longitude, time zone, a display name), kept in flash (NVS namespace `site`, shared by the charger and
 * Tesla setups: it belongs to the board). Read once at start-up so the clock and the daylight window start from it; saving a new one
 * restarts the board soon, the same way Tesla mode does. Until the owner saves one, the built-in default (Lathrop) is used.
 */
class SiteSettings : public solar::SiteControl {
public:
    void setBeforeRestart(std::function<void()> callback) { beforeRestart_ = std::move(callback); }

    /** Once at start-up, before the clock and the daylight window use the site. */
    void begin();
    /** Often: carries out a pending restart. */
    void loop();

    solar::Site current() const override { return site_; }
    bool isSet() const override { return set_; }
    bool restartPending() const override { return restartAt_ != 0; }
    void save(const solar::Site& site) override;

private:
    solar::Site site_;
    bool set_ = false;
    std::function<void()> beforeRestart_;
    uint32_t restartAt_ = 0;
};

}  // namespace device
