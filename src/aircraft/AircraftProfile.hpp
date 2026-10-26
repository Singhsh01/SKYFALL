// Data-driven aircraft profiles.
//
// PROVENANCE — read before quoting any number:
//   * Mass, wing area, span, length and engine thrust class are rounded public figures
//     (manufacturer fact sheets / widely published reference values). They are approximate.
//   * Aerodynamic coefficients (CL0, CL_alpha, CLmax, CD0, induced-drag factor), inertia-like
//     response times, rate limits and G limits are TUNED ILLUSTRATIVE VALUES chosen to give a
//     credible, flyable feel. They are not derived from, and do not claim to match, any
//     non-public or certified performance data. Nothing here is flight-certified.
#pragma once

#include <array>
#include <string>

namespace sky {

// The fighters come first (ids 0..kFighterCount-1): menus, the Academy, the campaign and the AI
// fighter pools only ever pick from them. Transports follow.
enum class AircraftId { F22, F35, Rafale, Su57, Su35, MiG29, Su30, F16, FA18, Typhoon, C17, Count };
constexpr int kAircraftCount = static_cast<int>(AircraftId::Count);
constexpr int kFighterCount = static_cast<int>(AircraftId::C17);

enum class AircraftRole { Fighter, Transport };

/// Visual family used by the procedural mesh builder (original geometry, not copied models).
enum class AirframeStyle { StealthTwinTail, StealthSingle, DeltaCanard, FlankerFamily, Fulcrum, ViperSingle, Hornet, Typhoon, Transport, Count };
constexpr int kAirframeStyleCount = static_cast<int>(AirframeStyle::Count);

struct AircraftProfile {
    AircraftId id;
    const char* name;
    const char* shortName;
    AirframeStyle style;
    bool playableNow;   // only one aircraft is fully tuned in this build (see docs/AIRCRAFT.md)

    // Mass properties
    double emptyMassKg;
    double fuelMassKg;        // internal fuel at mission start
    // Geometry (reference)
    double wingAreaM2;
    double spanM;
    double lengthM;
    // Aerodynamics (illustrative)
    double cl0;               // lift coefficient at zero AoA (camber/flap/body)
    double clAlpha;           // per radian
    double clMax;             // peak CL
    double alphaStallRad;     // AoA at CLmax
    double cd0;               // parasite drag
    double inducedK;          // CD = cd0 + k * CL^2
    double gearCd;            // extra parasite drag with gear down
    double airbrakeCd;
    // Propulsion (total for all engines)
    double thrustMilN;        // max dry ("military") thrust, sea level static
    double thrustAbN;         // max afterburner thrust, sea level static
    double spoolTimeS;        // engine response time constant
    double tsfcMil;           // kg/(N*h) fuel flow per thrust (approximate)
    double tsfcAb;
    // Handling (illustrative, assisted fly-by-wire)
    double maxRollRate;       // rad/s
    double maxPitchRate;      // rad/s
    double maxYawRate;        // rad/s
    double gLimit;            // positive load factor limit
    double alphaLimitRad;     // FBW AoA limiter (Assisted)
    double rotateSpeedMs;     // Vr
    double approachSpeedMs;
    double rateTau;           // first-order lag of body rates
    // Camera anchors (body frame, metres): x fwd, y up, z right
    double eyeX, eyeY;
    double chaseDistance, chaseHeight;
    // Arcade performance profile (Arcade handling only; Assisted / Simulation use the physics):
    // top speed as a Mach number at sea level and at 11 km (blended in between) and the time to
    // reach it from slow flight (150 m/s) in full afterburner
    double arcadeMachSL = 1.15, arcadeMachHigh = 1.9, arcadeAccelS = 3.5;
    // Loadout
    int missiles;
    int cannonRounds;
    int flareCharges;
    // Role: transports have no afterburner, no weapons, a low G limit and fly only in Free Flight
    // (and as AI aircraft in the Academy's logistics and escort missions)
    AircraftRole role = AircraftRole::Fighter;
    bool afterburner = true;
    bool carrierCapable = false;   // naval fighter (catapult + arrestor hook in the real aircraft)
};

inline bool isFighter(AircraftId id) { return static_cast<int>(id) < kFighterCount; }
inline bool isFighter(int id) { return id >= 0 && id < kFighterCount; }
/// The aircraft a combat mode flies: transports fall back to the F-22.
inline int combatAircraft(int id) { return isFighter(id) ? id : 0;}

const AircraftProfile& profile(AircraftId id);
const std::array<AircraftProfile, static_cast<size_t>(AircraftId::Count)>& allProfiles();

}  // namespace sky
