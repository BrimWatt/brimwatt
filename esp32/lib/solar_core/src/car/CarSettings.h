#pragma once

#include <string>

namespace solar {

/** What is kept across restarts about controlling through the car (flash on the board). */
class CarSettingsStore {
public:
    virtual ~CarSettingsStore() = default;
    virtual bool loadThrough() = 0;                 // is the car in command?
    virtual void saveThrough(bool car) = 0;
    virtual int loadOnReturn() = 0;                 // CarOnReturn as a number
    virtual void saveOnReturn(int value) = 0;
    virtual bool loadAllowWake() = 0;               // may Brimwatt wake a sleeping car? (off unless the owner switches it on)
    virtual void saveAllowWake(bool on) = 0;
    // Tesla mode (off unless the owner switches it on) and its hotspot password; the defaults suit stores that do not keep them.
    virtual bool loadTeslaMode() { return false; }
    virtual void saveTeslaMode(bool) {}
    virtual bool loadApiLink() { return false; }     // connect through the Tesla account (Fleet API) instead of Bluetooth
    virtual void saveApiLink(bool) {}
    virtual std::string loadHotspotPassword() { return ""; }
    virtual void saveHotspotPassword(const std::string&) {}
};

}  // namespace solar
