// Third-person chase camera rig: a spring arm behind the aircraft with an independent free look
// (360 degrees around the aircraft, above and below it, a quick look back), automatic or instant
// re-centring, and terrain collision. Pure math (no raylib), so it is unit-tested; the flight
// controls never pass through it, so looking around cannot move the aircraft.
#pragma once

#include "../sim/Math.hpp"

#include <functional>

namespace sky {

struct ChaseLookInput {
    double yaw = 0, pitch = 0;   // look deltas this frame (radians)
    bool looking = false;        // the player is actively looking (mouse drag, stick, keys)
    bool behind = false;         // look-back button held
    bool reset = false;          // snap the view back behind the aircraft
};

struct ChaseView {
    Vec3 pos, target, up;
    double lookAmount = 0;       // 0 = plain chase view .. 1 = looking well away from the nose
};

class ChaseRig {
public:
    static constexpr double kMaxPitch = 1.30;   // up to ~75 degrees above / below
    double autoRecentreDelay = 1.6;              // s after the last look input (0 = never)

    double lookYaw() const { return yaw_; }
    double lookPitch() const { return pitch_; }
    double behindBlend() const { return behind_; }
    double armLength() const { return arm_; }

    void reset() { yaw_ = pitch_ = 0; behind_ = 0; idle_ = 0; arm_ = -1; }

    void input(double dt, const ChaseLookInput& in, bool autoRecentre) {
        if (in.reset) { yaw_ = pitch_ = 0; behind_ = 0; idle_ = 0; return; }
        if (in.looking) {
            yaw_ = wrapPi(yaw_ + in.yaw);
            pitch_ = clampd(pitch_ + in.pitch, -kMaxPitch, kMaxPitch);
            idle_ = 0;
        } else {
            idle_ += dt;
            if (autoRecentre && autoRecentreDelay > 0 && idle_ > autoRecentreDelay) {
                double k = approachFactor(dt, 0.45);
                yaw_ = wrapPi(yaw_ - yaw_ * k);
                pitch_ -= pitch_ * k;
                if (std::fabs(yaw_) < 1e-4) yaw_ = 0;
                if (std::fabs(pitch_) < 1e-4) pitch_ = 0;
            }
        }
        // look back: a quick swing while the button is held, back again on release
        double target = in.behind ? 1.0 : 0.0;
        behind_ += (target - behind_) * approachFactor(dt, 0.09);
        if (std::fabs(behind_ - target) < 1e-3) behind_ = target;
    }

    /// pos / q: the aircraft and the (smoothed) frame the arm hangs from; dist / height: arm
    /// behind and above at zero look; ground(x, z): terrain surface height for collision.
    ChaseView solve(double dt, const Vec3& pos, const Quat& q, double dist, double height,
                    const std::function<double(double, double)>& ground, double clearance = 2.5) {
        double yaw = yaw_ + behind_ * kPi * (yaw_ >= 0 ? 1.0 : -1.0);
        double basePitch = std::atan2(height, dist);
        double armWanted = std::sqrt(dist * dist + height * height);
        // the view direction in the aircraft frame (+x nose, +y up, +z right wing)
        double pv = pitch_ - basePitch;
        Vec3 vLocal{std::cos(pv) * std::cos(yaw), std::sin(pv), std::cos(pv) * std::sin(yaw)};
        Vec3 v = normalize(q.rotate(vLocal));
        // spring arm against the terrain: shorten at once when blocked, ease back out
        double armFree = armWanted;
        if (ground) {
            const int steps = 14;
            for (int i = 1; i <= steps; ++i) {
                double t = static_cast<double>(i) / steps;
                Vec3 p = pos - v * (armWanted * t);
                if (p.y < ground(p.x, p.z) + clearance) { armFree = armWanted * std::max(0.18, (i - 1.0) / steps); break; }
            }
        }
        if (arm_ < 0 || armFree < arm_) arm_ = armFree;
        else arm_ += (armFree - arm_) * approachFactor(dt, 0.35);
        ChaseView out;
        out.pos = pos - v * arm_;
        if (ground) out.pos.y = std::max(out.pos.y, ground(out.pos.x, out.pos.z) + clearance);
        // aim a little above the flight path at zero look (the original chase framing); when
        // looking around, aim past the aircraft along the view direction
        Vec3 vAim = normalize(q.rotate(Vec3{std::cos(pitch_ + 0.09) * std::cos(yaw), std::sin(pitch_ + 0.09),
                                            std::cos(pitch_ + 0.09) * std::sin(yaw)}));
        out.target = pos + vAim * 45.0;
        out.lookAmount = clampd(std::max(std::fabs(wrapPi(yaw)) / 1.2, std::fabs(pitch_) / 0.8), 0.0, 1.0);
        Vec3 up = normalize(Vec3{0, 1, 0} * 0.05 + q.up() * 0.95);
        // looking far off the nose: keep the horizon steadier
        out.up = normalize(lerp(up, Vec3{0, 1, 0}, out.lookAmount * 0.6));
        return out;
    }

private:
    double yaw_ = 0, pitch_ = 0, behind_ = 0, idle_ = 0, arm_ = -1;
};

}  // namespace sky
