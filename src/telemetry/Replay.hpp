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
    int targetJet = -1;     // jet id the missile is guiding on (-1 = none / ground)
    int owner = -1;
};

/// A cannon round in flight (tracers in the kill replay).
struct RecTracer {
    Vec3 pos, dir;
    int owner = -1;
};

struct RecFrame {
    double t = 0;           // game clock (s)
    RecJet player;
    std::vector<RecJet> others;
    std::vector<RecMissile> missiles;
    std::vector<RecTracer> tracers;
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

/// Shared by the replays: spherical interpolation of orientations.
Quat slerpQuat(const Quat& a, Quat b, double t);

// ------------------------------------------------------------------------------------------
// Kill replay: the last seconds before the player shoots an enemy jet down, seen from the
// downed enemy's cockpit. The clip is a private copy of the recorder's frames, so the crash
// replay (which reads the live recorder) and the kill replay never share event data.
// ------------------------------------------------------------------------------------------
struct KillClip {
    int victimId = -1;
    AircraftId victimType = AircraftId::Su35;
    std::string victimName;
    double killTime = 0;
    Vec3 victimVel;                 // velocity at the kill (the wreck falls on from there)
    std::string weapon;             // "ATA", "GUN", ...
    std::vector<RecFrame> frames;   // copied window, oldest first
    bool valid() const { return victimId >= 0 && frames.size() >= 4; }
    double start() const { return frames.empty() ? killTime : frames.front().t; }
    /// Interpolated pose of a jet (player or other) at time t; false if it is not in the clip then.
    bool jetAt(int id, double t, RecJet& out) const;
    RecJet playerAt(double t) const;
    /// The victim's pose; after the kill, the wreck tumbles and falls from its last pose.
    RecJet victimAt(double t) const;
    /// Nearest frame at or before t.
    const RecFrame* frameAt(double t) const;
};

/// Copies the last `seconds` of the recorder for a kill. `victimAtKill` is the victim's pose at
/// the moment it was destroyed (appended as the final frame).
KillClip makeKillClip(const FlightRecorder& rec, int victimId, const RecJet& victimAtKill, const Vec3& victimVel, double killTime,
                      const std::string& victimName, const std::string& weapon, double seconds = 4.5);

/// The enemy pilot's view: from the cockpit, looking at the biggest threat (an incoming missile
/// guiding on it, else the player's jet when close, else ahead), zoomed so it reads in a small
/// window. `KillCamState` carries the smoothed view direction and zoom between calls.
struct KillCamState { Vec3 look; double fov = 0; };
struct KillView {
    Vec3 eye, target, up{0, 1, 0};
    double fovDeg = 58;
    double impact = 0;      // 0..1 flash right after the kill
    bool afterKill = false;
    bool threatVisible = false;
};
KillView killCamera(const KillClip& clip, double t, KillCamState& st, double dt);

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
