#pragma once

#include <cstdint>
#include <string>

namespace solar {

/** Tells whether solar-following modes should be active right now. */
class DaylightSchedule {
public:
    virtual ~DaylightSchedule() = default;

    virtual bool isOpen(int64_t epochSeconds) const = 0;

    /** Today's window as epoch seconds, for drawing it on a chart. False if there is none. */
    virtual bool windowFor(int64_t /*epochSeconds*/, int64_t& /*start*/, int64_t& /*end*/) const { return false; }

    /** Changes how long after sunrise the window opens and how long before sunset it closes. */
    virtual void setMargins(int /*startAfterSunriseSeconds*/, int /*endBeforeSunsetSeconds*/) {}

    /** Today's window as text for logs and the dashboard, e.g. "08:12 to 16:55". */
    virtual std::string describe(int64_t epochSeconds) const = 0;
};

/** For tests and for running without a schedule. */
class AlwaysOpen : public DaylightSchedule {
public:
    bool isOpen(int64_t) const override { return true; }
    std::string describe(int64_t) const override { return "always open"; }
};

}  // namespace solar
