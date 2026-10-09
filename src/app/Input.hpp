// Keyboard / gamepad / mouse mapping with smoothing, dead zones and response curves.
//
// Keyboard (default):
//   W / S          throttle up / down          Arrow Up / Down   pitch (Down = pull up)
//   Arrow L / R    roll                         A / D             yaw (rudder / nosewheel)
//   B              toggle parking brake         Left Ctrl (hold)  wheel brakes / airbrake
//   E              engine start                 G                 landing gear
//   Space / LMB    cannon                       F / RMB           missile
//   X              flares                       T / Tab           cycle target
//   C              camera                       H                 help overlay
//   R              reset / respawn              Esc / P           pause menu
//   F3             telemetry overlay            F12               screenshot
//   Mouse drag (right button) or I/J/K/L : look around / orbit,  wheel: zoom
#pragma once

#include "Settings.hpp"

namespace sky {

struct InputState {
    // continuous
    double pitch = 0, roll = 0, yaw = 0, throttleRate = 0;
    bool brake = false, airbrake = false, gun = false;
    double lookYaw = 0, lookPitch = 0, zoom = 0;  // per-frame deltas
    bool looking = false;
    // edges (pressed this frame)
    bool missile = false, flares = false, cycleTarget = false, gear = false, camera = false, engine = false,
         parkingBrake = false, help = false, reset = false, pause = false, screenshot = false, debug = false,
         confirm = false, back = false, up = false, down = false, left = false, right = false, timeOfDay = false;
};

class Input {
public:
    void update(double dt, const Settings& s);
    const InputState& state() const { return st_; }
    bool gamepadConnected() const { return pad_ >= 0; }

private:
    InputState st_;
    double kPitch_ = 0, kRoll_ = 0, kYaw_ = 0;
    int pad_ = -1;
};

}  // namespace sky
