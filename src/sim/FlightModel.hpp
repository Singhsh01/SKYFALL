// SKYFALL flight model — independent of raylib.
//
// What is modelled (see docs/PHYSICS.md for equations and the honest limits):
//   Translational 3-DOF rigid body with real forces:
//     L = 0.5*rho*V^2*S*CL(alpha), D = 0.5*rho*V^2*S*CD(alpha), side force from sideslip,
//     thrust (spooling engine, afterburner, ISA density lapse), gravity, ground reaction,
//     rolling/brake friction and lateral tyre friction.
//   Rotational 3-DOF via quaternions, driven by an "assisted fly-by-wire" control law
//     (G-command pitch, rate-command roll, sideslip-cancelling yaw) with a first-order body
//     rate response. Inertia and aerodynamic moments are lumped into that response; that is a
//     documented approximation, not a full moment-coefficient model.
//   Integrator: semi-implicit (symplectic) Euler at a fixed 120 Hz.
#pragma once

#include "../aircraft/AircraftProfile.hpp"
#include "Math.hpp"
#include "Terrain.hpp"

namespace sky {

enum class HandlingMode { Assisted, Simulation };
enum class EngineState { Off, Starting, Running, Flameout };
enum class CrashReason { None, HardLanding, WingStrike, BellyLanding, NoseFirst, Terrain, Water, Destroyed, TailStrike };

const char* crashReasonText(CrashReason r);

struct ControlInputs {
    double pitch = 0;      // -1..1, +1 = stick back (nose up)
    double roll = 0;       // -1..1, +1 = roll right
    double yaw = 0;        // -1..1, +1 = nose right (rudder / nosewheel)
    double throttleRate = 0;  // -1..1 lever motion command (W/S)
    bool brake = false;       // wheel brakes held
    bool airbrake = false;
};

struct FlightState {
    Vec3 pos;            // CG position, world, m
    Vec3 vel;            // world, m/s
    Quat q;              // body -> world
    Vec3 omega;          // body rates, rad/s (x roll, y yaw[+ = nose left], z pitch)
    double throttle = 0;     // lever 0..1 (>= kAbDetent = afterburner)
    double rpm = 0;          // engine core speed fraction 0..1 (idle ~0.62)
    double abLevel = 0;      // 0..1 afterburner light-up
    double thrustN = 0;
    EngineState engine = EngineState::Off;
    double startTimer = 0;
    bool gearDown = true;
    double gearPos = 1.0;    // 0 = up/locked, 1 = down/locked
    bool parkingBrake = true;
    bool wheelBrake = false;
    bool airbrake = false;
    double fuelKg = 0;
    bool onGround = true;
    bool crashed = false;
    CrashReason crashReason = CrashReason::None;
    // Derived telemetry (updated each step)
    double alpha = 0, beta = 0;   // rad
    double gLoad = 1.0;           // normal load factor (body up)
    double mach = 0;
    double airspeed = 0;          // true airspeed m/s
    double qbar = 0;              // dynamic pressure Pa
    double cl = 0, cd = 0;
    bool stalled = false;
    double agl = 0;               // height of CG above surface
    double timeAirborne = 0;
    double simTime = 0;
};

class FlightModel {
public:
    static constexpr double kDt = 1.0 / 120.0;
    static constexpr double kAbDetent = 0.9;
    static constexpr double kGearHeight = 2.35;  // CG above ground on extended gear (m)
    static constexpr double kBellyHeight = 1.1;

    FlightModel(const AircraftProfile& p, const Terrain& t) : prof_(&p), terrain_(&t) {}

    /// Cold & dark on the runway threshold, parking brake set, engine off.
    void resetOnRunway(bool engineRunning = false);
    /// Airborne, gear up, engine running at the given speed/heading (deg/rad documented).
    void resetAirborne(const Vec3& pos, double headingRad, double speedMs, double throttle = 0.75);

    void step(const ControlInputs& in, double dt = kDt);

    // Discrete commands
    void requestEngineStart();
    void toggleParkingBrake() { s_.parkingBrake = !s_.parkingBrake; }
    void setParkingBrake(bool on) { s_.parkingBrake = on; }
    /// Returns false (and refuses) when weight-on-wheels prevents gear retraction.
    bool toggleGear();
    void setHandling(HandlingMode m) { mode_ = m; }
    HandlingMode handling() const { return mode_; }
    void applyDamage(double) {}
    void forceCrash(CrashReason r);

    const FlightState& state() const { return s_; }
    FlightState& mutableState() { return s_; }
    const AircraftProfile& profile() const { return *prof_; }
    double massKg() const { return prof_->emptyMassKg + s_.fuelKg + 1200.0; }

    // Aerodynamic coefficient functions (exposed for tests & docs).
    double liftCoefficient(double alpha) const;
    double dragCoefficient(double alpha, double cl) const;

private:
    void updateEngine(const ControlInputs& in, double dt, double rho);
    void updateAttitude(const ControlInputs& in, double dt);
    void handleGround(const ControlInputs& in, double dt, Vec3& force);

    const AircraftProfile* prof_;
    const Terrain* terrain_;
    FlightState s_{};
    HandlingMode mode_ = HandlingMode::Assisted;
};

/// Fixed-step accumulator with a bounded catch-up (prevents the "spiral of death").
struct FixedStepper {
    double dt = FlightModel::kDt;
    double accumulator = 0;
    int maxStepsPerFrame = 10;   // 83 ms of simulation at most per rendered frame
    int droppedSteps = 0;
    /// Feed real elapsed time; returns how many fixed steps to run this frame.
    int advance(double frameSeconds) {
        if (frameSeconds < 0) frameSeconds = 0;
        accumulator += frameSeconds;
        int steps = static_cast<int>(accumulator / dt);
        if (steps > maxStepsPerFrame) {
            droppedSteps += steps - maxStepsPerFrame;
            steps = maxStepsPerFrame;
            accumulator = 0;  // discard backlog after a stall
        } else {
            accumulator -= steps * dt;
        }
        return steps;
    }
    double alpha() const { return accumulator / dt; }  // interpolation factor for rendering
};

}  // namespace sky
