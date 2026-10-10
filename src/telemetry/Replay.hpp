// Flight recorder + crash analysis (raylib-free, unit-tested).
//
// The game records the last few seconds of the battle at 30 Hz. When the player is lost, the
// renderer replays them from side-observer cameras, and `analyzeLoss` turns the recorded
// telemetry into short, concrete feedback ("you were rolled 110 degrees when you pulled").
#pragma once

#include "../aircraft/AircraftProfile.hpp"
#include "../sim/FlightModel.hpp"
#include "../sim/Math.hpp"

#include <deque>
#include <string>
#include <vector>

namespace sky {

struct RecJet {
    int id = -1;
    AircraftId type = AircraftId::F22;
    int team = 0;           // 0 blue, 1 red
    Vec3 pos;
    Quat q;
    float gear = 0, throttle = 0, ab = 0;
};

struct RecMissile {
    int id = -1;
    Vec3 pos, dir;
    bool motor = false;
    bool atPlayer = false;
};

struct RecFrame {
    double t = 0;           // game clock (s)
    RecJet player;
    std::vector<RecJet> others;
    std::vector<RecMissile> missiles;
    float speed = 0, vs = 0, agl = 0, gLoad = 1;
    float pitchDeg = 0, bankDeg = 0, throttle = 0;
    bool gearDown = false, gpws = false, missileWarning = false, flaresActive = false, stalled = false;
};

class FlightRecorder {
public:
    static constexpr double kRate = 30.0;      // frames per second
    static constexpr double kWindow = 10.0;    // seconds kept

    /// Returns true if a frame was stored (rate-limited to kRate).
    bool offer(const RecFrame& f);
    void clear() { frames_.clear(); }
    const std::deque<RecFrame>& frames() const { return frames_; }
    bool empty() const { return frames_.empty(); }
    double startTime() const { return frames_.empty() ? 0.0 : frames_.front().t; }
    double endTime() const { return frames_.empty() ? 0.0 : frames_.back().t; }
    /// Interpolated player pose at time t (clamped to the recorded window).
    RecJet playerAt(double t) const;
    /// Nearest recorded frame at or before t.
    const RecFrame* frameAt(double t) const;

private:
    std::deque<RecFrame> frames_;
};

enum class LossCause { Crash, ShotDownMissile, ShotDownGun, Other };

struct LossReport {
    std::string title;                 // e.g. "CONTROLLED FLIGHT INTO TERRAIN"
    std::vector<std::string> facts;    // measured values at impact
    std::vector<std::string> advice;   // what to do differently
};

/// Builds human-readable feedback from the recording. `deathTime` is the game time of loss.
LossReport analyzeLoss(const FlightRecorder& rec, double deathTime, CrashReason reason, LossCause cause,
                       const std::string& killer);

/// Minimum altitude (m) needed to pull out of a dive of `gammaDeg` (negative) at speed v (m/s)
/// with a sustained load factor n: R = v^2 / (g (n-1)), h = R (1 - cos gamma).
double pullOutAltitude(double v, double gammaDeg, double n);

}  // namespace sky
