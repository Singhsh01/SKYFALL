// Picture-in-picture camera state (shared by the game loop and the HUD).
#pragma once

#include "../sim/Math.hpp"
#include "raylib.h"

namespace sky {

enum class PipMode { Off, Target, Missile, Hit };

/// Picture-in-picture camera state: target camera while locking, missile camera after launch,
/// and a 2.8 s "hit cam" hold on the impact point.
struct PipState {
    PipMode mode = PipMode::Off;
    int missileId = -1;
    int targetJet = -1, targetGround = -1;
    double timer = 0, age = 0, flash = 0;
    Vec3 lastPos, lastDir{1, 0, 0}, lastTarget;
    bool haveTarget = false;
    double lastDist = 1e9, missDist = 0, launchRange = 0;
    int result = 0;            // 0 missed, 1 hit, 2 destroyed
    bool ats = false;
    Vec3 holdPos, holdLook;
    Camera3D cam{};
};

}  // namespace sky
