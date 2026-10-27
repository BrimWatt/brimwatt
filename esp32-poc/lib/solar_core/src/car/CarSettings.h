#pragma once

namespace solar {

/** What is kept across restarts about controlling through the car (flash on the board). */
class CarSettingsStore {
public:
    virtual ~CarSettingsStore() = default;
    virtual bool loadThrough() = 0;                 // is the car in command?
    virtual void saveThrough(bool car) = 0;
    virtual int loadOnReturn() = 0;                 // CarOnReturn as a number
    virtual void saveOnReturn(int value) = 0;
};

}  // namespace solar
