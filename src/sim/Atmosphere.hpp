// International Standard Atmosphere (troposphere + lower stratosphere), SI units.
#pragma once
#include <cmath>

namespace sky {

struct AtmosphereSample {
    double temperatureK;
    double pressurePa;
    double density;       // kg/m^3
    double speedOfSound;  // m/s
};

/// ISA up to 20 km. Valid quantity, not a heuristic (ISO 2533:1975).
inline AtmosphereSample isa(double altitudeM) {
    constexpr double T0 = 288.15, P0 = 101325.0, L = 0.0065, R = 287.05287, g0 = 9.80665, gamma = 1.4;
    double h = altitudeM < -500 ? -500 : (altitudeM > 20000 ? 20000 : altitudeM);
    AtmosphereSample s{};
    if (h <= 11000.0) {
        s.temperatureK = T0 - L * h;
        s.pressurePa = P0 * std::pow(s.temperatureK / T0, g0 / (L * R));
    } else {
        const double T11 = T0 - L * 11000.0;
        const double P11 = P0 * std::pow(T11 / T0, g0 / (L * R));
        s.temperatureK = T11;
        s.pressurePa = P11 * std::exp(-g0 * (h - 11000.0) / (R * T11));
    }
    s.density = s.pressurePa / (R * s.temperatureK);
    s.speedOfSound = std::sqrt(gamma * R * s.temperatureK);
    return s;
}

}  // namespace sky
