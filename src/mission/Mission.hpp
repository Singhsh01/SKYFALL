// Reusable mission finite-state machine. Steps advance ONLY when a telemetry predicate has
// held true for its required duration — elapsed time alone never completes a step.
#pragma once

#include "../combat/Combat.hpp"
#include "../sim/FlightModel.hpp"

#include <functional>
#include <string>
#include <vector>

namespace sky {

struct MissionContext {
    const FlightState* flight = nullptr;
    const AircraftProfile* aircraft = nullptr;
    const Terrain* terrain = nullptr;
    int enemiesDestroyed = 0;
    const Battle* battle = nullptr;   // campaign objectives (targets / bandits alive)
};

struct MissionStep {
    std::string title;
    std::string instruction;
    std::string keyHint;
    std::function<bool(const MissionContext&)> done;
    double holdSeconds = 0.0;
    std::function<double(const MissionContext&)> progress;  // optional 0..1 progress bar
};

enum class MissionStatus { Running, Complete, Failed };

class Mission {
public:
    Mission(std::string id, std::string title, std::vector<MissionStep> steps)
        : id_(std::move(id)), title_(std::move(title)), steps_(std::move(steps)) {}

    void update(const MissionContext& ctx, double dt);
    /// Checked every update while running; returning a non-empty reason fails the mission.
    void addFailCondition(std::function<std::string(const MissionContext&)> f) { failIf_.push_back(std::move(f)); }
    void fail(const std::string& why);
    void reset();
    /// Restarts the mission at step `idx` (checkpoint retries): running again, earlier steps kept done.
    void skipTo(size_t idx);

    MissionStatus status() const { return status_; }
    size_t stepIndex() const { return index_; }
    size_t stepCount() const { return steps_.size(); }
    const MissionStep* current() const { return index_ < steps_.size() ? &steps_[index_] : nullptr; }
    const std::vector<MissionStep>& steps() const { return steps_; }
    double holdTimer() const { return hold_; }
    double elapsed() const { return elapsed_; }
    double stepFlash() const { return flash_; }   // >0 right after a step completes (UI feedback)
    const std::string& failure() const { return failure_; }
    const std::string& title() const { return title_; }
    const std::string& id() const { return id_; }
    const std::vector<double>& completionTimes() const { return times_; }

private:
    std::string id_, title_;
    std::vector<MissionStep> steps_;
    size_t index_ = 0;
    double hold_ = 0, elapsed_ = 0, flash_ = 0;
    MissionStatus status_ = MissionStatus::Running;
    std::string failure_;
    std::vector<double> times_;
    std::vector<std::function<std::string(const MissionContext&)>> failIf_;
};

/// Mission 01 — Cold Start & Takeoff.
Mission makeMission01();
/// Mission 02 — First Solo Flight (attitude, turns, climb, level-off, power).
Mission makeMission02();
/// Mission 03 — Runway Landing (gear, 3-degree glide path, touchdown, stop).
Mission makeMission03();
/// Ideal 3-degree glide-path height above the runway for a horizontal distance to the threshold.
double glidePathHeight(double distanceToThreshold);

/// Campaign catalogue: 61 missions, all playable (01-03 hand-written training, 04-51 from
/// data in Campaign.cpp).
struct MissionInfo {
    int number;
    std::string title;
    std::string summary;
    bool playable;
};
const std::vector<MissionInfo>& campaignCatalogue();
constexpr int kCampaignMissions = 61;

/// Compass heading in degrees (0 = north = world -Z, 90 = east = world +X).
double compassHeadingDeg(const Quat& q);

}  // namespace sky
