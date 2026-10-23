#include "Input.hpp"

#include "raylib.h"

#include <algorithm>
#include <cmath>

namespace sky {

namespace {
double approach(double v, double target, double rate, double dt) {
    double d = target - v;
    double step = rate * dt;
    if (std::fabs(d) <= step) return target;
    return v + (d > 0 ? step : -step);
}
double shape(double v, double deadzone, double sens) {
    double a = std::fabs(v);
    if (a < deadzone) return 0;
    a = (a - deadzone) / (1.0 - deadzone);
    a = 0.35 * a + 0.65 * a * a * a;  // expo curve: fine control near centre
    return std::clamp((v < 0 ? -a : a) * sens, -1.0, 1.0);
}
}  // namespace

void Input::update(double dt, const Settings& s) {
    InputState n;
    now_ += dt;
    // --- keyboard axes with smoothing (keyboards are digital) ---
    double kp = 0, kr = 0, ky = 0;
    if (IsKeyDown(KEY_DOWN)) kp += 1;
    if (IsKeyDown(KEY_UP)) kp -= 1;
    if (IsKeyDown(KEY_RIGHT)) kr += 1;
    if (IsKeyDown(KEY_LEFT)) kr -= 1;
    if (IsKeyDown(KEY_D)) ky += 1;
    if (IsKeyDown(KEY_A)) ky -= 1;
    if (s.invertPitch) kp = -kp;
    // keyboard keys reach full deflection in ~0.18 s (pitch) / ~0.13 s (roll), and the stick
    // recentres twice as fast; reversing a key passes through centre at the recentre rate
    auto key = [&](double v, double target, double press, double release) {
        bool reversing = target != 0 && v * target < 0;
        return approach(v, target, target == 0 || reversing ? release : press, dt);
    };
    kPitch_ = key(kPitch_, kp * s.sensitivity, 5.5, 10.0);
    kRoll_ = key(kRoll_, kr * s.sensitivity, 7.5, 14.0);
    kYaw_ = key(kYaw_, ky, 5.0, 9.0);
    n.pitch = std::clamp(kPitch_, -1.0, 1.0);
    n.roll = std::clamp(kRoll_, -1.0, 1.0);
    n.yaw = kYaw_;
    if (IsKeyDown(KEY_W)) n.throttleRate += 1;
    if (IsKeyDown(KEY_S)) n.throttleRate -= 1;
    n.brake = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    n.airbrake = n.brake;
    n.gun = IsKeyDown(KEY_SPACE) || IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    n.missile = IsKeyPressed(KEY_F) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE);
    n.flares = IsKeyPressed(KEY_X);
    n.cycleTarget = IsKeyPressed(KEY_T) || IsKeyPressed(KEY_TAB);
    n.gear = IsKeyPressed(KEY_G);
    n.camera = IsKeyPressed(KEY_C);
    n.engine = IsKeyPressed(KEY_E);
    n.parkingBrake = IsKeyPressed(KEY_B);
    n.help = IsKeyPressed(KEY_H);
    n.reset = IsKeyPressed(KEY_R);
    n.pause = IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P);
    n.screenshot = IsKeyPressed(KEY_F12);
    n.debug = IsKeyPressed(KEY_F3);
    n.timeOfDay = IsKeyPressed(KEY_F5);
    n.weaponToggle = IsKeyPressed(KEY_Q);
    n.selectATA = IsKeyPressed(KEY_ONE);
    n.selectATS = IsKeyPressed(KEY_TWO);
    n.hudToggle = IsKeyPressed(KEY_U);
    // double-tap Left/Right arrow -> full aileron roll
    if (IsKeyPressed(KEY_LEFT)) { if (now_ - lastLeft_ < 0.3) { n.barrelRoll = -1; lastLeft_ = -10; } else lastLeft_ = now_; }
    if (IsKeyPressed(KEY_RIGHT)) { if (now_ - lastRight_ < 0.3) { n.barrelRoll = 1; lastRight_ = -10; } else lastRight_ = now_; }
    n.confirm = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
    n.back = IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE);
    n.up = IsKeyPressed(KEY_UP);
    n.down = IsKeyPressed(KEY_DOWN);
    n.left = IsKeyPressed(KEY_LEFT);
    n.right = IsKeyPressed(KEY_RIGHT);

    // --- mouse look / zoom ---
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 d = GetMouseDelta();
        n.lookYaw = d.x * 0.004;
        n.lookPitch = -d.y * 0.004;
        n.looking = true;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && IsKeyDown(KEY_LEFT_SHIFT)) n.missile = true;
    if (IsKeyDown(KEY_J)) { n.lookYaw -= 1.6 * dt; n.looking = true; }
    if (IsKeyDown(KEY_L)) { n.lookYaw += 1.6 * dt; n.looking = true; }
    if (IsKeyDown(KEY_I)) { n.lookPitch += 1.2 * dt; n.looking = true; }
    if (IsKeyDown(KEY_K)) { n.lookPitch -= 1.2 * dt; n.looking = true; }
    n.zoom = GetMouseWheelMove();

    // --- gamepad (first connected) ---
    pad_ = -1;
    for (int i = 0; i < 4; ++i) if (IsGamepadAvailable(i)) { pad_ = i; break; }
    if (pad_ >= 0) {
        double lx = GetGamepadAxisMovement(pad_, GAMEPAD_AXIS_LEFT_X);
        double ly = GetGamepadAxisMovement(pad_, GAMEPAD_AXIS_LEFT_Y);
        double rx = GetGamepadAxisMovement(pad_, GAMEPAD_AXIS_RIGHT_X);
        double ry = GetGamepadAxisMovement(pad_, GAMEPAD_AXIS_RIGHT_Y);
        double rt = (GetGamepadAxisMovement(pad_, GAMEPAD_AXIS_RIGHT_TRIGGER) + 1.0) * 0.5;
        double lt = (GetGamepadAxisMovement(pad_, GAMEPAD_AXIS_LEFT_TRIGGER) + 1.0) * 0.5;
        double gp = shape(ly, s.deadzone, s.sensitivity) * (s.invertPitch ? -1 : 1);
        double gr = shape(lx, s.deadzone, s.sensitivity);
        if (std::fabs(gp) > std::fabs(n.pitch)) n.pitch = gp;
        if (std::fabs(gr) > std::fabs(n.roll)) n.roll = gr;
        if (rt > 0.1) n.throttleRate = rt;
        if (lt > 0.1) n.throttleRate = -lt;
        if (IsGamepadButtonDown(pad_, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) n.yaw = -1;
        if (IsGamepadButtonDown(pad_, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) n.yaw = 1;
        if (std::fabs(rx) > 0.15 || std::fabs(ry) > 0.15) { n.lookYaw += rx * 2.0 * dt; n.lookPitch -= ry * 1.5 * dt; n.looking = true; }
        n.gun = n.gun || IsGamepadButtonDown(pad_, GAMEPAD_BUTTON_RIGHT_TRIGGER_1);
        n.missile = n.missile || IsGamepadButtonPressed(pad_, GAMEPAD_BUTTON_LEFT_TRIGGER_1);
        n.flares = n.flares || IsGamepadButtonPressed(pad_, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        n.cycleTarget = n.cycleTarget || IsGamepadButtonPressed(pad_, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
        n.gear = n.gear || IsGamepadButtonPressed(pad_, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
        n.camera = n.camera || IsGamepadButtonPressed(pad_, GAMEPAD_BUTTON_RIGHT_FACE_UP);
        n.parkingBrake = n.parkingBrake || IsGamepadButtonPressed(pad_, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
        n.engine = n.engine || IsGamepadButtonPressed(pad_, GAMEPAD_BUTTON_LEFT_FACE_UP);
        n.pause = n.pause || IsGamepadButtonPressed(pad_, GAMEPAD_BUTTON_MIDDLE_RIGHT);
        n.confirm = n.confirm || IsGamepadButtonPressed(pad_, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        n.back = n.back || IsGamepadButtonPressed(pad_, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
        n.up = n.up || IsGamepadButtonPressed(pad_, GAMEPAD_BUTTON_LEFT_FACE_UP);
        n.down = n.down || IsGamepadButtonPressed(pad_, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
    }
    st_ = n;
}

}  // namespace sky
