#include "Mission.hpp"

#include <algorithm>
#include <cmath>

namespace sky {

double compassHeadingDeg(const Quat& q) {
    Euler e = toEuler(q);
    double deg = 90.0 + e.heading / kDeg;
    while (deg < 0) deg += 360.0;
    while (deg >= 360.0) deg -= 360.0;
    return deg;
}

void Mission::reset() {
    index_ = 0; hold_ = 0; elapsed_ = 0; flash_ = 0;
    status_ = MissionStatus::Running;
    failure_.clear();
    times_.clear();
}

void Mission::fail(const std::string& why) {
    if (status_ != MissionStatus::Running) return;
    status_ = MissionStatus::Failed;
    failure_ = why;
}

void Mission::update(const MissionContext& ctx, double dt) {
    flash_ = std::max(0.0, flash_ - dt);
    if (status_ != MissionStatus::Running) return;
    elapsed_ += dt;
    if (ctx.flight && ctx.flight->crashed) {
        fail(crashReasonText(ctx.flight->crashReason));
        return;
    }
    for (auto& f : failIf_) {
        std::string why = f(ctx);
        if (!why.empty()) { fail(why); return; }
    }
    // Allow several quick steps to resolve in one frame, but each needs its own predicate.
    for (int guard = 0; guard < 4 && index_ < steps_.size(); ++guard) {
        const MissionStep& s = steps_[index_];
        if (s.done && s.done(ctx)) {
            hold_ += dt;
            if (hold_ + 1e-9 >= s.holdSeconds) {
                times_.push_back(elapsed_);
                ++index_;
                hold_ = 0;
                flash_ = 1.6;
                continue;
            }
        } else {
            hold_ = 0;
        }
        break;
    }
    if (index_ >= steps_.size()) status_ = MissionStatus::Complete;
}

Mission makeMission01() {
    std::vector<MissionStep> st;
    st.push_back({"ENGINE START", "Start the engine and wait for it to spool up to a stable idle.", "E",
                  [](const MissionContext& c) { return c.flight->engine == EngineState::Running; }, 0.0,
                  [](const MissionContext& c) { return std::min(1.0, c.flight->rpm / 0.62); }});
    st.push_back({"RELEASE PARKING BRAKE", "Release the parking brake.", "B",
                  [](const MissionContext& c) { return !c.flight->parkingBrake; }, 0.0, nullptr});
    st.push_back({"THROTTLE TO MIL POWER", "Advance the throttle to at least 85% and let the engine spool.", "HOLD W",
                  [](const MissionContext& c) { return c.flight->throttle >= 0.85 && c.flight->rpm >= 0.9; }, 0.0,
                  [](const MissionContext& c) { return std::min(1.0, c.flight->throttle / 0.85); }});
    st.push_back({"TAKEOFF ROLL", "Track the centreline (A / D) and accelerate to rotation speed.", "A / D",
                  [](const MissionContext& c) { return c.flight->airspeed >= c.aircraft->rotateSpeedMs * 0.97; }, 0.0,
                  [](const MissionContext& c) { return std::min(1.0, c.flight->airspeed / (c.aircraft->rotateSpeedMs * 0.97)); }});
    st.push_back({"ROTATE", "Pull back smoothly to raise the nose to about 10 degrees.", "DOWN ARROW",
                  [](const MissionContext& c) { return toEuler(c.flight->q).pitch >= 8.0 * kDeg || !c.flight->onGround; }, 0.0,
                  [](const MissionContext& c) { return clampd(toEuler(c.flight->q).pitch / (8.0 * kDeg), 0.0, 1.0); }});
    st.push_back({"POSITIVE CLIMB", "Lift off and establish a positive rate of climb.", "",
                  [](const MissionContext& c) { return !c.flight->onGround && c.flight->vel.y > 2.5 && c.flight->agl > 15.0; }, 1.0,
                  [](const MissionContext& c) { return clampd(c.flight->agl / 15.0, 0.0, 1.0); }});
    st.push_back({"GEAR UP", "Retract the landing gear before 150 m/s.", "G",
                  [](const MissionContext& c) { return !c.flight->gearDown && c.flight->gearPos <= 0.0; }, 0.0,
                  [](const MissionContext& c) { return c.flight->gearDown ? 0.0 : 1.0 - c.flight->gearPos; }});
    st.push_back({"CLIMB & STABILISE", "Climb above 300 m AGL, wings level, heading 090 +/-10 for 5 seconds.", "",
                  [](const MissionContext& c) {
                      Euler e = toEuler(c.flight->q);
                      double hdg = compassHeadingDeg(c.flight->q);
                      return c.flight->agl > 300.0 && std::fabs(e.bank) < 10.0 * kDeg && std::fabs(hdg - 90.0) < 10.0 &&
                             !c.flight->onGround;
                  }, 5.0,
                  [](const MissionContext& c) { return clampd(c.flight->agl / 300.0, 0.0, 1.0); }});
    return Mission("M01", "COLD START & TAKEOFF", std::move(st));
}

Mission makeMission02() {
    std::vector<MissionStep> st;
    auto e = [](const MissionContext& c) { return toEuler(c.flight->q); };
    st.push_back({"STRAIGHT & LEVEL", "Wings level, hold altitude within 60 m for 4 seconds.", "",
                  [e](const MissionContext& c) { return std::fabs(e(c).bank) < 8 * kDeg && std::fabs(c.flight->vel.y) < 4.0 && c.flight->pos.y > 900; }, 4.0, nullptr});
    st.push_back({"BANK LEFT 45", "Roll left to about 45 degrees of bank and hold it.", "LEFT ARROW",
                  [e](const MissionContext& c) { double b = e(c).bank / kDeg; return b < -35 && b > -60; }, 3.0,
                  [e](const MissionContext& c) { return clampd(-e(c).bank / (40 * kDeg), 0.0, 1.0); }});
    st.push_back({"BANK RIGHT 45", "Roll through to about 45 degrees right and hold it.", "RIGHT ARROW",
                  [e](const MissionContext& c) { double b = e(c).bank / kDeg; return b > 35 && b < 60; }, 3.0,
                  [e](const MissionContext& c) { return clampd(e(c).bank / (40 * kDeg), 0.0, 1.0); }});
    st.push_back({"TURN TO HEADING 270", "Bank and pull to turn onto heading 270, then roll wings level.", "ARROWS",
                  [e](const MissionContext& c) {
                      double h = compassHeadingDeg(c.flight->q);
                      return std::fabs(h - 270.0) < 10.0 && std::fabs(e(c).bank) < 15 * kDeg;
                  }, 3.0,
                  [](const MissionContext& c) { return clampd(1.0 - std::fabs(compassHeadingDeg(c.flight->q) - 270.0) / 180.0, 0.0, 1.0); }});
    st.push_back({"CLIMB TO 2000 M", "Pitch up and add power to climb through 2000 m.", "DOWN ARROW / W",
                  [](const MissionContext& c) { return c.flight->pos.y >= 2000.0; }, 0.0,
                  [](const MissionContext& c) { return clampd((c.flight->pos.y - 1200.0) / 800.0, 0.0, 1.0); }});
    st.push_back({"LEVEL OFF", "Level off between 1900 and 2300 m: vertical speed under 3 m/s for 4 s.", "",
                  [](const MissionContext& c) { return std::fabs(c.flight->vel.y) < 3.0 && c.flight->pos.y > 1900 && c.flight->pos.y < 2300; }, 4.0, nullptr});
    st.push_back({"CRUISE POWER", "Set 60-80% throttle and hold 180-260 m/s for 3 s.", "W / S",
                  [](const MissionContext& c) { return c.flight->throttle > 0.54 && c.flight->throttle < 0.72 && c.flight->airspeed > 180 && c.flight->airspeed < 260; }, 3.0, nullptr});
    return Mission("M02", "FIRST SOLO FLIGHT", std::move(st));
}

double glidePathHeight(double d) { return std::max(0.0, d) * std::tan(3.0 * kDeg); }

Mission makeMission03() {
    std::vector<MissionStep> st;
    auto dist = [](const MissionContext& c) { return c.terrain->runway().threshold().x - c.flight->pos.x; };
    st.push_back({"GEAR DOWN", "Lower the landing gear for the approach.", "G",
                  [](const MissionContext& c) { return c.flight->gearPos >= 1.0; }, 0.0,
                  [](const MissionContext& c) { return c.flight->gearPos; }});
    st.push_back({"ON THE GLIDE PATH", "Fly the 3-degree glide path (HUD 'GS') at 75-105 m/s for 4 s.", "W / S",
                  [dist](const MissionContext& c) {
                      double h = c.flight->pos.y - c.terrain->runway().center.y;
                      return std::fabs(h - glidePathHeight(dist(c))) < 45.0 && c.flight->airspeed > 75 && c.flight->airspeed < 105;
                  }, 4.0, nullptr});
    st.push_back({"TOUCHDOWN", "Flare gently and touch down on the runway.", "",
                  [](const MissionContext& c) {
                      return c.flight->onGround && c.terrain->onRunway(c.flight->pos.x, c.flight->pos.z, 5.0);
                  }, 0.0, nullptr});
    st.push_back({"BRAKE TO A STOP", "Throttle idle and hold the wheel brakes until stopped.", "S + LEFT CTRL",
                  [](const MissionContext& c) { return c.flight->onGround && c.flight->airspeed < 2.0; }, 0.0,
                  [](const MissionContext& c) { return clampd(1.0 - c.flight->airspeed / 80.0, 0.0, 1.0); }});
    return Mission("M03", "RUNWAY LANDING", std::move(st));
}

}  // namespace sky
