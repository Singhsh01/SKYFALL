// Camera modes (raylib-free so cycling logic is unit-testable).
#pragma once

namespace sky {

enum class CameraMode { Cockpit = 0, Chase = 1, External = 2, Count = 3 };

inline CameraMode nextCamera(CameraMode m) {
    return static_cast<CameraMode>((static_cast<int>(m) + 1) % static_cast<int>(CameraMode::Count));
}
inline const char* cameraName(CameraMode m) {
    switch (m) {
        case CameraMode::Cockpit: return "COCKPIT";
        case CameraMode::Chase: return "CHASE";
        case CameraMode::External: return "EXTERNAL";
        default: return "?";
    }
}

}  // namespace sky
