#include "AircraftProfile.hpp"

#include "../sim/Math.hpp"

namespace sky {

namespace {
// Helper so each row stays readable.
AircraftProfile make(AircraftId id, const char* name, const char* shortName, AirframeStyle style, bool playable,
                     double empty, double fuel, double S, double span, double len, double milKN, double abKN,
                     double rollDeg, double pitchDeg, double g, double vr, double eyeX, double chaseD, int msl) {
    AircraftProfile p{};
    p.id = id; p.name = name; p.shortName = shortName; p.style = style; p.playableNow = playable;
    p.emptyMassKg = empty; p.fuelMassKg = fuel;
    p.wingAreaM2 = S; p.spanM = span; p.lengthM = len;
    p.cl0 = 0.12; p.clAlpha = 4.2; p.clMax = 1.55; p.alphaStallRad = 26.0 * kDeg;
    p.cd0 = 0.021; p.inducedK = 0.13; p.gearCd = 0.018; p.airbrakeCd = 0.06;
    p.thrustMilN = milKN * 1000.0; p.thrustAbN = abKN * 1000.0;
    p.spoolTimeS = 1.6; p.tsfcMil = 0.08; p.tsfcAb = 0.20;
    p.maxRollRate = rollDeg * kDeg; p.maxPitchRate = pitchDeg * kDeg; p.maxYawRate = 12.0 * kDeg;
    p.gLimit = g; p.alphaLimitRad = 24.0 * kDeg; p.rotateSpeedMs = vr; p.approachSpeedMs = vr * 1.05;
    p.rateTau = 0.18;
    p.eyeX = eyeX; p.eyeY = 1.05;
    p.chaseDistance = chaseD * 0.82; p.chaseHeight = 0.30 * chaseD * 0.82;
    p.missiles = msl; p.cannonRounds = 480; p.flareCharges = 3;
    return p;
}

const std::array<AircraftProfile, static_cast<size_t>(AircraftId::Count)> kProfiles = {
    //   id                name                 short     style                         playable empty   fuel   S     span  len   mil   AB   roll pitch g   Vr   eyeX chase msl
    make(AircraftId::F22,    "F-22 Raptor",        "F-22",  AirframeStyle::StealthTwinTail, true,  19700, 8200, 78.0, 13.6, 18.9, 140, 232, 220, 32, 9.0, 72, 5.6, 26, 6),
    make(AircraftId::F35,    "F-35 Lightning II",  "F-35",  AirframeStyle::StealthSingle,   false, 13300, 8300, 42.7, 10.7, 15.7, 125, 191, 200, 28, 9.0, 74, 4.8, 23, 4),
    make(AircraftId::Rafale, "Dassault Rafale",    "RAFALE",AirframeStyle::DeltaCanard,     false,  9900, 4700, 45.7, 10.9, 15.3, 100, 150, 240, 32, 9.0, 68, 4.6, 22, 6),
    make(AircraftId::Su57,   "Su-57 Felon",        "SU-57", AirframeStyle::StealthTwinTail, false, 18000, 8000, 78.8, 14.1, 20.1, 176, 284, 220, 32, 9.0, 72, 6.0, 27, 6),
    make(AircraftId::Su35,   "Su-35",              "SU-35", AirframeStyle::FlankerFamily,   false, 18400, 8000, 62.0, 15.3, 21.9, 172, 284, 210, 30, 9.0, 74, 6.4, 28, 6),
    make(AircraftId::MiG29,  "MiG-29",             "MIG-29",AirframeStyle::Fulcrum,         false, 11000, 3500, 38.0, 11.4, 17.4, 100, 163, 220, 30, 9.0, 70, 5.4, 23, 4),
    make(AircraftId::Su30,   "Su-30",              "SU-30", AirframeStyle::FlankerFamily,   false, 18400, 7800, 62.0, 14.7, 21.9, 149, 246, 200, 28, 9.0, 74, 6.0, 28, 6),
    make(AircraftId::F16,    "F-16 Fighting Falcon","F-16", AirframeStyle::ViperSingle,     false,  8600, 3200, 27.9, 10.0, 15.1,  76, 129, 240, 30, 9.0, 66, 4.0, 20, 4),
};
}  // namespace

const AircraftProfile& profile(AircraftId id) { return kProfiles[static_cast<size_t>(id)]; }
const std::array<AircraftProfile, static_cast<size_t>(AircraftId::Count)>& allProfiles() { return kProfiles; }

}  // namespace sky
