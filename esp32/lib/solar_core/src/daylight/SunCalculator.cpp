#include "daylight/SunCalculator.h"

#include <cmath>

#include "common/CivilDate.h"

namespace solar {

namespace {

constexpr double kPi = 3.14159265358979323846;
/** Zenith for sunrise and sunset: 90 degrees plus atmospheric refraction and the sun's radius. */
constexpr double kSunriseZenithDegrees = 90.833;

double radians(double degrees) { return degrees * kPi / 180.0; }
double degrees(double radians) { return radians * 180.0 / kPi; }

double mod360(double d) {
    double r = std::fmod(d, 360.0);
    return r < 0 ? r + 360 : r;
}

}  // namespace

SunTimes calculateSun(int64_t epochDay, double latitude, double longitude,
                      const std::function<int(int64_t)>& utcOffset) {
    // Julian date of local solar noon, roughly, and the Julian century used by the formulas.
    double julianDay = static_cast<double>(epochDay) + 2440587.5 + 0.5 - longitude / 360.0;
    double t = (julianDay - 2451545.0) / 36525.0;

    double meanLongitude = mod360(280.46646 + t * (36000.76983 + t * 0.0003032));
    double meanAnomaly = 357.52911 + t * (35999.05029 - 0.0001537 * t);
    double eccentricity = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);

    double m = radians(meanAnomaly);
    double equationOfCenter = std::sin(m) * (1.914602 - t * (0.004817 + 0.000014 * t)) +
                              std::sin(2 * m) * (0.019993 - 0.000101 * t) + std::sin(3 * m) * 0.000289;
    double trueLongitude = meanLongitude + equationOfCenter;
    double omega = 125.04 - 1934.136 * t;
    double apparentLongitude = trueLongitude - 0.00569 - 0.00478 * std::sin(radians(omega));

    double meanObliquity = 23 + (26 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60) / 60;
    double obliquity = meanObliquity + 0.00256 * std::cos(radians(omega));
    double declination = std::asin(std::sin(radians(obliquity)) * std::sin(radians(apparentLongitude)));

    double y = std::pow(std::tan(radians(obliquity / 2)), 2);
    double l0 = radians(meanLongitude);
    double equationOfTimeMinutes =
        4 * degrees(y * std::sin(2 * l0) - 2 * eccentricity * std::sin(m) +
                    4 * eccentricity * y * std::sin(m) * std::cos(2 * l0) - 0.5 * y * y * std::sin(4 * l0) -
                    1.25 * eccentricity * eccentricity * std::sin(2 * m));

    double solarNoonMinutesUtc = 720 - 4 * longitude - equationOfTimeMinutes;
    int64_t noon = epochDay * 86400 + std::llround(solarNoonMinutesUtc * 60.0);
    // Solar noon computed from the UTC day can fall on the neighbouring local date far from Greenwich.
    int64_t actualDay = floorDiv(noon + utcOffset(noon), 86400);
    if (actualDay < epochDay) noon += 86400;
    if (actualDay > epochDay) noon -= 86400;

    SunTimes sun;
    sun.noon = noon;

    double lat = radians(latitude);
    double cosHourAngle = std::cos(radians(kSunriseZenithDegrees)) / (std::cos(lat) * std::cos(declination)) -
                          std::tan(lat) * std::tan(declination);
    if (cosHourAngle > 1) {
        sun.kind = SunTimes::Kind::AlwaysDown;
        return sun;
    }
    if (cosHourAngle < -1) {
        sun.kind = SunTimes::Kind::AlwaysUp;
        return sun;
    }

    double hourAngleDegrees = degrees(std::acos(cosHourAngle));
    int64_t halfDaySeconds = std::llround(hourAngleDegrees * 4 * 60);   // 4 minutes of time per degree
    sun.kind = SunTimes::Kind::Normal;
    sun.sunrise = noon - halfDaySeconds;
    sun.sunset = noon + halfDaySeconds;
    return sun;
}

}  // namespace solar
