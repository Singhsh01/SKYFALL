#include "FlightModel.hpp"

#include "Atmosphere.hpp"

#include <algorithm>
#include <cmath>

namespace sky {

const char* crashReasonText(CrashReason r) {
    switch (r) {
        case CrashReason::None: return "";
        case CrashReason::HardLanding: return "HARD LANDING - sink rate exceeded gear limits";
        case CrashReason::WingStrike: return "WING STRIKE - excessive bank at touchdown";
        case CrashReason::BellyLanding: return "BELLY LANDING - gear was not extended";
        case CrashReason::NoseFirst: return "NOSE-FIRST IMPACT";
        case CrashReason::Terrain: return "CONTROLLED FLIGHT INTO TERRAIN";
        case CrashReason::Water: return "IMPACT WITH WATER";
        case CrashReason::Destroyed: return "AIRCRAFT DESTROYED";
        case CrashReason::TailStrike: return "TAIL STRIKE";
        case CrashReason::Obstacle: return "COLLISION WITH AN OBSTACLE";
    }
    return "";
}

double FlightModel::liftCoefficient(double alpha) const {
    const AircraftProfile& p = *prof_;
    // Linear region with a smooth cap at CLmax, then a post-stall decay towards a
    // flat-plate model CL = 1.1*sin(2a). Symmetric for negative AoA (reduced magnitude).
    double linear = p.cl0 + p.clAlpha * alpha;
    double a = std::fabs(alpha);
    double flat = 1.1 * std::sin(2.0 * alpha);
    double peak = alpha >= 0 ? std::min(linear, p.clMax) : std::max(linear, -p.clMax * 0.75);
    if (a <= p.alphaStallRad) return peak;
    double t = smoothstep(p.alphaStallRad, p.alphaStallRad + 14.0 * kDeg, a);
    return lerpd(peak, flat * 0.85, t);
}

double FlightModel::dragCoefficient(double alpha, double cl) const {
    const AircraftProfile& p = *prof_;
    double cd = p.cd0 + p.inducedK * cl * cl + p.gearCd * s_.gearPos + (s_.airbrake ? p.airbrakeCd : 0.0);
    double a = std::fabs(alpha);
    if (a > p.alphaStallRad) cd += 1.4 * std::pow(std::sin(a), 2.0) * smoothstep(p.alphaStallRad, p.alphaStallRad + 10 * kDeg, a);
    return cd;
}

FlightModel::LandingAxisDir FlightModel::landingDir(const DeckInfo& d) {
    // direction of the angled deck's landing line (deck coordinates (190, -30), see DeckInfo)
    Vec3 f = d.forward(), r{-f.z, 0.0, f.x};
    Vec3 dir = normalize(f * 190.0 - r * 30.0);
    return {dir, std::atan2(dir.z, dir.x)};
}

void FlightModel::resetOnRunway(bool engineRunning) {
    const RunwayInfo& rw = terrain_->runway();
    s_ = FlightState{};
    Vec3 start = rw.threshold();
    s_.pos = {start.x, rw.center.y + kGearHeight, start.z};
    s_.vel = {0, 0, 0};
    s_.q = Quat::fromEuler(rw.heading, 0.0, 0.0);
    s_.fuelKg = prof_->fuelMassKg;
    s_.gearDown = true;
    s_.gearPos = 1.0;
    s_.parkingBrake = true;
    s_.onGround = true;
    s_.agl = kGearHeight;
    if (engineRunning) {
        s_.engine = EngineState::Running;
        s_.rpm = 0.62;
    }
}

void FlightModel::resetOnCatapult() {
    const DeckInfo& d = terrain_->deck();
    s_ = FlightState{};
    Vec3 start = d.catapultStart();
    s_.pos = {start.x, d.center.y + kGearHeight, start.z};
    s_.q = Quat::fromEuler(d.heading, 0.0, 0.0);
    s_.fuelKg = prof_->fuelMassKg;
    s_.gearDown = true;
    s_.gearPos = 1.0;
    s_.parkingBrake = true;
    s_.onGround = true;
    s_.agl = kGearHeight;
    s_.engine = EngineState::Running;
    s_.rpm = 0.62;
    s_.catArmed = true;
}

double FlightModel::catapultEndSpeed() const { return std::max(78.0, prof_->rotateSpeedMs * 1.3); }

void FlightModel::resetAirborne(const Vec3& pos, double headingRad, double speedMs, double throttle) {
    s_ = FlightState{};
    s_.pos = pos;
    s_.q = Quat::fromEuler(headingRad, 0.0, 0.0);
    s_.vel = s_.q.forward() * speedMs;
    s_.fuelKg = prof_->fuelMassKg;
    s_.gearDown = false;
    s_.gearPos = 0.0;
    s_.parkingBrake = false;
    s_.onGround = false;
    s_.engine = EngineState::Running;
    s_.throttle = throttle;
    s_.rpm = 0.62 + 0.38 * std::min(throttle / kAbDetent, 1.0);
    // Pre-trim AoA so the aircraft starts in 1 g flight instead of sinking.
    double rho = isa(pos.y).density;
    double qbar = 0.5 * rho * speedMs * speedMs;
    double clNeed = massKg() * kG / (qbar * prof_->wingAreaM2);
    double alpha = clampd((clNeed - prof_->cl0) / prof_->clAlpha, -0.1, 0.3);
    s_.q = Quat::fromEuler(headingRad, alpha, 0.0);
    s_.vel = Quat::fromEuler(headingRad, 0.0, 0.0).forward() * speedMs;
}

void FlightModel::requestEngineStart() {
    if (s_.engine == EngineState::Off || s_.engine == EngineState::Flameout) {
        if (s_.fuelKg <= 0) return;
        s_.engine = EngineState::Starting;
        s_.startTimer = 0;
    }
}

bool FlightModel::toggleGear() {
    if (s_.gearDown && s_.onGround) return false;  // weight-on-wheels interlock
    s_.gearDown = !s_.gearDown;
    return true;
}

void FlightModel::forceCrash(CrashReason r) {
    if (s_.crashed) return;
    s_.crashed = true;
    s_.crashReason = r;
    s_.engine = EngineState::Off;
    s_.abLevel = 0;
}

void FlightModel::updateEngine(const ControlInputs& in, double dt, double rho) {
    const AircraftProfile& p = *prof_;
    // the lever moves faster in Arcade (idle to full afterburner in about 0.7 s)
    const bool boost = mode_ == HandlingMode::Arcade && arcadeBoost_;
    s_.throttle = clampd(s_.throttle + in.throttleRate * (boost ? 1.45 : 0.55) * dt, 0.0, 1.0);
    constexpr double kIdle = 0.62;
    switch (s_.engine) {
        case EngineState::Off:
        case EngineState::Flameout:
            s_.rpm += (0.0 - s_.rpm) * approachFactor(dt, 3.0);
            s_.abLevel += (0.0 - s_.abLevel) * approachFactor(dt, 0.2);
            break;
        case EngineState::Starting: {
            s_.startTimer += dt;
            constexpr double kStart = 6.5;  // compressed for gameplay (real starts take longer)
            s_.rpm = std::max(s_.rpm, kIdle * smoothstep(0.0, kStart, s_.startTimer));
            if (s_.startTimer >= kStart) s_.engine = EngineState::Running;
            break;
        }
        case EngineState::Running: {
            double target = kIdle + (1.0 - kIdle) * std::min(s_.throttle / kAbDetent, 1.0);
            // on the catapult the run-up is quicker (deck crews do not wait for a slow spool)
            double spool = (target > s_.rpm ? p.spoolTimeS : p.spoolTimeS * 0.6) * (s_.catArmed ? 0.35 : 1.0) * (boost ? 0.3 : 1.0);
            s_.rpm += (target - s_.rpm) * approachFactor(dt, spool);
            double abTarget = (s_.throttle >= kAbDetent && s_.rpm > 0.96)
                                  ? 0.55 + 0.45 * (s_.throttle - kAbDetent) / (1.0 - kAbDetent)
                                  : 0.0;
            s_.abLevel += (abTarget - s_.abLevel) * approachFactor(dt, boost ? 0.15 : 0.35);
            break;
        }
    }
    double milFrac = 0.0;
    if (s_.engine == EngineState::Running || s_.engine == EngineState::Starting) {
        milFrac = 0.05 + 0.95 * std::pow(clampd((s_.rpm - kIdle) / (1.0 - kIdle), 0.0, 1.0), 1.3);
        if (s_.rpm < kIdle * 0.9) milFrac *= s_.rpm / kIdle * 0.5;
    }
    double lapse = std::pow(rho / 1.225, 0.7) * (1.0 + 0.25 * std::min(s_.mach, 1.6));
    s_.thrustN = (p.thrustMilN * milFrac + (p.thrustAbN - p.thrustMilN) * s_.abLevel) * lapse;
    if (s_.engine == EngineState::Running) {
        double tsfc = lerpd(p.tsfcMil, p.tsfcAb, s_.abLevel);
        s_.fuelKg -= tsfc * s_.thrustN / 3600.0 * dt;
        if (s_.fuelKg <= 0) {
            s_.fuelKg = 0;
            s_.engine = EngineState::Flameout;
        }
    }
}

double FlightModel::arcadeTopSpeed(double altitude) const {
    const AircraftProfile& p = *prof_;
    double a = isa(std::max(0.0, altitude)).speedOfSound;
    return a * lerpd(p.arcadeMachSL, p.arcadeMachHigh, smoothstep(0.0, 11000.0, altitude));
}

void FlightModel::arcadeVelocity(const ControlInputs& in, double dt, double rho) {
    const AircraftProfile& p = *prof_;
    const double m = massKg();
    double V = length(s_.vel);
    Vec3 dir = s_.vel / V;
    Vec3 fwd = s_.q.forward();
    // speed along the path: thrust - drag - gravity - energy bleed from manoeuvring
    double qS = 0.5 * rho * V * V * p.wingAreaM2;
    double cd = (p.cd0 * 3.0 + p.gearCd * s_.gearPos + (s_.airbrake ? p.airbrakeCd * 2.5 : 0.0));
    double turnRate = std::sqrt(s_.omega.z * s_.omega.z + s_.omega.y * s_.omega.y);
    double cosA = clampd(dot(fwd, dir), -1.0, 1.0);
    // arcade guarantees a full-afterburner thrust/weight of at least 1.25 for every aircraft
    double thrustScale = std::max(1.0, 1.25 * m * kG / std::max(p.thrustAbN, 1.0));
    double accel = (s_.thrustN * thrustScale * cosA - qS * cd) / m - kG * dir.y - 0.045 * turnRate * V;
    if (arcadeBoost_) {
        // Arcade performance profile: the speed chases a target set by the lever, at rates that
        // take a jet from slow flight to its arcade top speed in ~3-4 s in full afterburner and
        // back to a controllable speed in ~4-5 s at idle with the airbrake. Independent of the
        // weapons load. Below the boost band (slow flight, gear down) the physics above applies,
        // so the jet can still slow down for a landing and never stops or reverses in the air.
        const double vmax = arcadeTopSpeed(s_.pos.y);
        const double vIdle = 0.30 * vmax, vMil = 0.74 * vmax;
        const double vCtrl = std::max(p.approachSpeedMs * 1.6, 0.36 * vmax);   // "controllable" speed after braking
        double dry = clampd(s_.throttle / kAbDetent, 0.0, 1.0);
        bool running = s_.engine == EngineState::Running;
        double rpmFrac = clampd((s_.rpm - 0.62) / (1.0 - 0.62), 0.0, 1.0);   // 0.62 = idle rpm (updateEngine)
        double vt = running ? lerpd(vIdle, vMil, std::pow(std::min(dry, rpmFrac + 0.02), 1.15)) + (vmax - vMil) * clampd(s_.abLevel, 0.0, 1.0) : 0.0;
        vt *= 1.0 - 0.30 * std::max(0.0, dir.y) + 0.10 * std::max(0.0, -dir.y);   // climbs cost speed, dives add a little
        vt *= cosA > 0.0 ? 0.6 + 0.4 * cosA : 0.6;                                   // flying sideways / high alpha bleeds
        if (s_.airbrake) vt = std::min(vt, vCtrl);
        const double accelMax = std::max(25.0, (vmax - 150.0) / std::max(1.0, p.arcadeAccelS));
        const double decelMax = std::max(15.0, (vmax - vCtrl) / (s_.airbrake ? 4.5 : 13.0));
        double dv = vt - V;
        double knee = 0.06 * vmax;   // soft approach to the target: no jolt when it is reached
        double aBoost = dv > 0 ? accelMax * std::min(1.0, dv / knee) : -decelMax * std::min(1.0, -dv / knee);
        aBoost += -kG * dir.y * 0.5 - 0.03 * turnRate * V;   // gravity along the path and turn bleed still felt
        // blend in above slow flight, with the gear up
        double band = std::max(p.approachSpeedMs * 1.35, 105.0);
        double w = smoothstep(band, band + 35.0, V) * (1.0 - s_.gearPos);
        if (!running) w = 0.0;
        double a = accel + (aBoost - accel) * w;
        // braking never takes the jet below the controllable speed on its own
        if (w > 0.0 && a < 0.0 && V + a * dt < vCtrl && vt >= vCtrl * 0.98) a = std::max(a, (vCtrl - V) / dt);
        accel = a;
    }
    V = std::max(0.0, V + accel * dt);
    // the flight path follows the nose; authority fades below ~60 m/s (arcade "stall")
    double auth = smoothstep(32.0, 75.0, V);
    double k = 4.5 * auth;
    Vec3 newDir = normalize(dir + (fwd - dir) * (1.0 - std::exp(-k * dt)));
    s_.vel = newDir * V;
    // below flying speed gravity takes over again
    s_.vel.y -= kG * (1.0 - auth) * dt;
    if (arcadeBoost_) {
        // arcade G follows the stick: full pull at fighting speed is the jet's G limit at any
        // speed (turn rate x speed would read tens of g at the arcade top speeds)
        double pmax = p.maxPitchRate * 2.05;
        double frac = clampd(std::fabs(s_.omega.z) / std::max(pmax, 1e-6), 0.0, 1.0);
        double yawFrac = clampd(std::fabs(s_.omega.y) / 0.62, 0.0, 1.0) * 0.25;
        double pull = std::min(1.0, frac + yawFrac) * (p.gLimit - 1.0) * std::min(1.0, V / 180.0);
        s_.gLoad = s_.omega.z >= 0 ? 1.0 + pull : 1.0 - pull * 0.45;
    } else {
        s_.gLoad = 1.0 + turnRate * V / kG * (s_.omega.z >= 0 ? 1.0 : -1.0) * (std::fabs(s_.omega.z) / std::max(turnRate, 1e-6));
    }
    s_.stalled = auth < 0.25;
}

void FlightModel::updateAttitude(const ControlInputs& in, double dt) {
    const AircraftProfile& p = *prof_;
    const double V = std::max(s_.airspeed, 1.0);
    Euler e = toEuler(s_.q);

    if (s_.onGround) {
        // Ground attitude: no bank, pitch only via rotation once the tail has authority.
        double rotateAuthority = smoothstep(0.62 * p.rotateSpeedMs, p.rotateSpeedMs, V);
        double pitchRate = in.pitch > 0 ? in.pitch * 0.16 * rotateAuthority : 0.0;
        if (pitchRate < 0.01 && e.pitch > 0) pitchRate = -0.12;  // nose settles back onto the gear
        double pitch = clampd(e.pitch + pitchRate * dt, 0.0, 15.0 * kDeg);
        // Nosewheel steering at low speed, rudder at high speed.
        double steer = in.yaw * (0.45 * (1.0 - smoothstep(10.0, 45.0, V)) + 0.05);
        double speedFactor = smoothstep(0.0, 3.0, V);
        double heading = e.heading + steer * speedFactor * dt;
        s_.q = Quat::fromEuler(heading, pitch, 0.0);
        s_.omega = {0.0, -steer * speedFactor, pitchRate};
        return;
    }

    if (mode_ == HandlingMode::Arcade) {
        double auth = 0.35 + 0.65 * smoothstep(40.0, 120.0, V);
        // per-aircraft arcade rates derived from the profile (F-22: ~280 deg/s roll, ~66 deg/s pitch)
        double rollRate = p.maxRollRate * 1.28, pitchRate = p.maxPitchRate * 2.05;
        Vec3 target{in.roll * rollRate * auth, -in.yaw * 0.62 * auth, in.pitch * pitchRate * (in.pitch > 0 ? 1.0 : 0.7) * auth};
        s_.omega = s_.omega + (target - s_.omega) * approachFactor(dt, 0.07);
        s_.q = s_.q.integrated(s_.omega, dt);
        // when too slow the nose falls through towards the flight path (weathervane)
        double weak = 1.0 - smoothstep(32.0, 75.0, V);
        if (weak > 0.0 && V > 1.0) {
            Vec3 fwd = s_.q.forward();
            Vec3 dir = normalize(s_.vel + Vec3{0, -15.0, 0});
            Vec3 axis = cross(fwd, dir);
            double ang = std::asin(clampd(length(axis), 0.0, 1.0));
            if (ang > 1e-4) {
                Quat r = Quat::fromAxisAngle(axis, std::min(ang, weak * 1.4 * dt));
                s_.q = (r * s_.q).normalized();
            }
        }
        return;
    }

    // ---- Airborne: assisted fly-by-wire control laws ----
    const double rho = isa(s_.pos.y).density;
    const double qbar = 0.5 * rho * V * V;
    const double eff = clampd(qbar / 7400.0, 0.12, 1.0);   // control-surface authority vs dynamic pressure
    const double W = massKg() * kG;
    const double S = p.wingAreaM2;
    const Vec3 velDir = normalize(s_.vel);
    const double gamma = std::asin(clampd(velDir.y, -1.0, 1.0));
    const double cphi = std::cos(e.bank);
    const bool assisted = mode_ == HandlingMode::Assisted;

    // Neutral stick holds the flight path; Assisted also adds turn compensation in banks < 70 deg.
    double n0 = std::cos(gamma) * cphi;
    if (assisted && std::fabs(e.bank) < 70.0 * kDeg) n0 = std::cos(gamma) / std::max(cphi, 0.35);
    n0 = clampd(n0, -2.0, p.gLimit);
    double nCmd = in.pitch >= 0 ? n0 + in.pitch * (p.gLimit - n0) : n0 + in.pitch * (n0 + 3.0);
    double clCmd = nCmd * W / std::max(qbar * S, 1.0);
    double alphaHi = assisted ? p.alphaLimitRad : 45.0 * kDeg;
    double alphaCmd = clampd((clCmd - p.cl0) / p.clAlpha, -12.0 * kDeg, alphaHi);

    // Feed-forward only the load factor the wing can actually deliver at alphaCmd, so the
    // AoA limiter is not overridden when the G request is unattainable (low speed).
    double nAvail = liftCoefficient(alphaCmd) * qbar * S / W;
    double nAch = nCmd >= 0 ? std::min(nCmd, nAvail) : std::max(nCmd, -nAvail);
    double qSteady = (nAch * kG - kG * std::cos(gamma) * cphi) / std::max(V, 40.0);
    double pitchRateT = qSteady + 4.0 * (alphaCmd - s_.alpha);
    double pMax = p.maxPitchRate * std::max(eff, 0.25);
    pitchRateT = clampd(pitchRateT, -pMax, pMax);

    double rollRateT = in.roll * p.maxRollRate * eff;
    double noseRightT = in.yaw * p.maxYawRate * eff + (assisted ? 2.5 : 0.9) * s_.beta * eff;

    double tau = p.rateTau / std::max(eff, 0.35);
    if (s_.stalled && !assisted) {
        // Post-stall: reduced authority, nose drop and a deterministic wing drop.
        pitchRateT = pitchRateT * 0.35 - 0.35;
        rollRateT = rollRateT * 0.4 + (s_.beta >= 0 ? 0.5 : -0.5);
        tau *= 1.8;
    }
    Vec3 target{rollRateT, -noseRightT, pitchRateT};
    double k = approachFactor(dt, tau);
    s_.omega = s_.omega + (target - s_.omega) * k;
    s_.q = s_.q.integrated(s_.omega, dt);
}

void FlightModel::step(const ControlInputs& in, double dt) {
    if (s_.crashed) return;
    const AircraftProfile& p = *prof_;
    s_.simTime += dt;
    s_.wheelBrake = in.brake;
    s_.airbrake = in.airbrake;

    // Gear actuator
    double gearTarget = s_.gearDown ? 1.0 : 0.0;
    double gearStep = dt / 3.5;
    if (std::fabs(gearTarget - s_.gearPos) <= gearStep) s_.gearPos = gearTarget;
    else s_.gearPos += gearTarget > s_.gearPos ? gearStep : -gearStep;

    AtmosphereSample atm = isa(s_.pos.y);
    const double m = massKg();
    double V = length(s_.vel);
    s_.airspeed = V;
    s_.mach = V / atm.speedOfSound;
    updateEngine(in, dt, atm.density);

    // Aerodynamic angles from body-frame velocity.
    Vec3 vb = s_.q.unrotate(s_.vel);
    if (V > 1.0) {
        s_.alpha = std::atan2(-vb.y, vb.x);
        s_.beta = std::asin(clampd(vb.z / V, -1.0, 1.0));
    } else {
        s_.alpha = s_.beta = 0;
    }
    s_.qbar = 0.5 * atm.density * V * V;
    s_.cl = liftCoefficient(s_.alpha);
    s_.cd = dragCoefficient(s_.alpha, s_.cl);
    s_.stalled = !s_.onGround && std::fabs(s_.alpha) > p.alphaStallRad && V > 5.0;

    Vec3 fAero{0, 0, 0};
    if (V > 0.5) {
        Vec3 velDir = s_.vel / V;
        Vec3 up = s_.q.up();
        Vec3 right = s_.q.right();
        Vec3 liftDir = normalize(up - velDir * dot(up, velDir));
        Vec3 sideDir = normalize(right - velDir * dot(right, velDir));
        double qS = s_.qbar * p.wingAreaM2;
        fAero += liftDir * (qS * s_.cl);
        fAero -= velDir * (qS * s_.cd);
        fAero += sideDir * (qS * -0.9 * s_.beta);
    }
    Vec3 fThrust = s_.q.forward() * s_.thrustN;
    Vec3 fGravity{0, -m * kG, 0};
    Vec3 fTotal = fAero + fThrust + fGravity;
    s_.gLoad = dot(fAero + fThrust, s_.q.up()) / (m * kG);

    // Semi-implicit Euler: velocity first, then position with the new velocity.
    if (mode_ == HandlingMode::Arcade && !s_.onGround && V > 1.0) arcadeVelocity(in, dt, atm.density);
    else s_.vel += fTotal / m * dt;

    // Catapult: the holdback keeps the jet still; brake release with afterburner fires the shot.
    const DeckInfo& deck = terrain_->deck();
    if (s_.catArmed) {
        // fires once the afterburner is lit (engine spooled up) and the brake is off
        if (!s_.parkingBrake && s_.throttle >= kAbDetent - 1e-3 && s_.abLevel > 0.5 && s_.engine == EngineState::Running) {
            s_.catArmed = false;
            s_.catFiring = true;
            s_.catTravel = 0;
        } else {
            s_.vel = {0, 0, 0};
        }
    }
    if (s_.catFiring) {
        Vec3 f = deck.forward();
        double v = dot(s_.vel, f);
        double vEnd = catapultEndSpeed();
        // constant acceleration that reaches vEnd at the end of the stroke
        double a = std::max(15.0, (vEnd * vEnd - v * v) / (2.0 * std::max(5.0, deck.catStroke - s_.catTravel)));
        v = std::min(vEnd, v + a * dt);
        s_.vel = f * v;
        s_.q = Quat::fromEuler(deck.heading, 0.0, 0.0);
        s_.catTravel += v * dt;
        if (s_.catTravel >= deck.catStroke) {
            s_.catFiring = false;
            s_.launchAssist = 3.5;
            s_.launched = true;
        }
    }

    // Ground friction (applied as a velocity change for unconditional stability).
    if (s_.onGround && !s_.catFiring) {
        double normalForce = std::max(0.0, -fTotal.y);
        Vec3 fwd = s_.q.forward();
        Vec3 fwdH = normalize(Vec3{fwd.x, 0, fwd.z});
        double vAlong = dot(s_.vel, fwdH);
        Vec3 vSide = Vec3{s_.vel.x, 0, s_.vel.z} - fwdH * vAlong;
        double mu = 0.022;
        if (s_.parkingBrake) mu = 0.9;
        else if (s_.wheelBrake) mu = 0.5;
        if (s_.gearPos < 0.9) mu = 0.65;
        double dv = mu * normalForce / m * dt;
        if (std::fabs(vAlong) <= dv) vAlong = 0;
        else vAlong -= (vAlong > 0 ? dv : -dv);
        vSide = vSide * (1.0 - std::min(1.0, 10.0 * dt));
        s_.vel = Vec3{fwdH.x * vAlong + vSide.x, s_.vel.y, fwdH.z * vAlong + vSide.z};
    }
    // Arresting wire: constant deceleration that stops the jet ~95 m after the catch,
    // whatever the throttle (pilots go to full power on touchdown in case the hook skips).
    if (s_.arresting) {
        LandingAxisDir ld = landingDir(deck);
        double v = std::max(0.0, dot(s_.vel, ld.dir));
        double decel = std::max(9.0, s_.arrestV0 * s_.arrestV0 / (2.0 * 95.0));
        v = std::max(0.0, v - decel * dt);
        s_.vel = ld.dir * v;
        Euler e = toEuler(s_.q);
        s_.q = Quat::fromEuler(e.heading + wrapPi(ld.heading - e.heading) * std::min(1.0, 2.0 * dt), std::max(0.0, e.pitch - 0.2 * dt), 0.0);
        if (v < 0.3) {
            s_.vel = {0, 0, 0};
            s_.arresting = false;
            s_.trapped = true;
            s_.parkingBrake = true;
        }
    } else if (s_.trapped) {
        s_.vel = {0, 0, 0};
    }
    s_.pos += s_.vel * dt;
    s_.airspeed = length(s_.vel);

    // Surface contact
    bool onDeck = terrain_->onDeck(s_.pos.x, s_.pos.z, 14.0);   // same footprint as surfaceHeight()
    bool runway = terrain_->onRunway(s_.pos.x, s_.pos.z, 15.0) || onDeck;
    double groundY = onDeck ? deck.center.y : runway ? terrain_->runway().center.y : terrain_->surfaceHeight(s_.pos.x, s_.pos.z);
    double contact = s_.gearPos > 0.9 ? kGearHeight : kBellyHeight;
    s_.agl = s_.pos.y - groundY;
    bool wasOnGround = s_.onGround;
    if (s_.agl <= contact) {
        bool water = !runway && terrain_->isWater(s_.pos.x, s_.pos.z);
        if (water) { forceCrash(CrashReason::Water); return; }
        if (!wasOnGround) {
            Euler e = toEuler(s_.q);
            double sink = -s_.vel.y;
            s_.touchdownSink = sink;
            s_.touchdownTime = s_.simTime;
            if (s_.gearPos < 0.9 && s_.airspeed > 20.0) { forceCrash(CrashReason::BellyLanding); return; }
            if (sink > 7.0) { forceCrash(CrashReason::HardLanding); return; }
            if (std::fabs(e.bank) > 15.0 * kDeg) { forceCrash(CrashReason::WingStrike); return; }
            if (e.pitch < -6.0 * kDeg) { forceCrash(CrashReason::NoseFirst); return; }
            if (e.pitch > 16.0 * kDeg && sink > 2.0) { forceCrash(CrashReason::TailStrike); return; }
            if (!runway) {
                Vec3 n = terrain_->normal(s_.pos.x, s_.pos.z, 10.0);
                if (n.y < 0.95 && s_.airspeed > 25.0) { forceCrash(CrashReason::Terrain); return; }
            }
        }
        s_.pos.y = groundY + contact;
        if (s_.vel.y < 0) s_.vel.y = 0;
        s_.onGround = true;
        s_.agl = contact;
        s_.airspeed = length(s_.vel);
    } else if (s_.onGround && s_.agl > contact + 0.05) {
        s_.onGround = false;
    }
    if (!s_.onGround) s_.timeAirborne += dt;

    // Tailhook and wires (only when a carrier deck exists).
    if (deck.active) {
        s_.hookDown = s_.gearDown;
        Vec3 fwd = s_.q.forward();
        Vec3 fwdH = normalize(Vec3{fwd.x, 0, fwd.z});
        Vec3 hook = s_.pos - fwdH * 6.5;
        double t, lat;
        deck.landingLine(hook.x, hook.z, t, lat);
        bool hookOnDeck = onDeck && s_.agl <= kGearHeight + 0.8;
        if (s_.hookDown && hookOnDeck && !s_.arresting && !s_.trapped && std::fabs(lat) < 12.0 && s_.airspeed > 15.0) {
            for (int w = 0; w < 4; ++w) {
                if (s_.hookT < deck.wireT[w] && t >= deck.wireT[w]) {
                    s_.wireCaught = w + 1;
                    s_.arresting = true;
                    s_.arrestV0 = std::max(s_.airspeed, 20.0);
                    break;
                }
            }
        }
        bool deckNow = s_.onGround && onDeck && !s_.arresting && !s_.trapped && !s_.catArmed && !s_.catFiring;
        if (s_.deckTouch && !s_.onGround && !s_.trapped && !s_.arresting) ++s_.bolters;   // touch and go: bolter
        s_.deckTouch = deckNow;
        s_.hookT = t;
        s_.hookLateral = lat;
        if (s_.arresting || s_.trapped) {
            s_.pos.y = deck.center.y + kGearHeight;
            s_.onGround = true;
            s_.agl = kGearHeight;
        }
    }

    // Airframe extremity collision (nose, wingtips, tails) against terrain while airborne.
    if (!s_.onGround) {
        const Vec3 pts[5] = {{p.lengthM * 0.5, 0, 0}, {0, 0, p.spanM * 0.5}, {0, 0, -p.spanM * 0.5},
                             {-p.lengthM * 0.45, 2.5, 0}, {0, -1.4, 0}};
        for (const Vec3& b : pts) {
            Vec3 w = s_.pos + s_.q.rotate(b);
            bool dk = terrain_->onDeck(w.x, w.z, 14.0);
            bool rw = terrain_->onRunway(w.x, w.z, 15.0) || dk;
            double gy = dk ? deck.center.y : rw && terrain_->onRunway(w.x, w.z, 15.0) ? terrain_->runway().center.y : terrain_->surfaceHeight(w.x, w.z);
            if (w.y < gy && !(rw && s_.agl < 4.0)) {
                forceCrash(terrain_->isWater(w.x, w.z) && !rw ? CrashReason::Water : CrashReason::Terrain);
                return;
            }
            if (terrain_->obstacleHit(w)) { forceCrash(CrashReason::Obstacle); return; }
        }
        if (terrain_->obstacleHit(s_.pos)) { forceCrash(CrashReason::Obstacle); return; }
    }

    updateAttitude(in, dt);

    // Hands-off carrier launch: the flight controls rotate to a climb attitude after the shot
    // unless the pilot is already pulling.
    if (s_.launchAssist > 0 && !s_.onGround) {
        s_.launchAssist -= dt;
        Euler e = toEuler(s_.q);
        if (e.pitch < 11.0 * kDeg && in.pitch < 0.3) {
            double rate = 0.22 * clampd((11.0 * kDeg - e.pitch) / (6.0 * kDeg), 0.0, 1.0);
            s_.q = s_.q.integrated(Vec3{0, 0, rate}, dt);
            if (mode_ != HandlingMode::Arcade) s_.vel = s_.vel + s_.q.up() * (0.4 * kG * dt);
        }
        // wings level
        if (std::fabs(e.bank) > 2.0 * kDeg && std::fabs(in.roll) < 0.1) s_.q = s_.q.integrated(Vec3{-e.bank * 1.5, 0, 0}, dt);
    }
}

}  // namespace sky
