// Adaptive rendering: holds the frame-time budget first by lowering the 3D scene resolution
// (10 % steps, down to 50 %), then, only if that is not enough, by lowering scene detail (view
// distances of trees, buildings and fine terrain; shadows off at the lower levels). Restores in
// the opposite order once there is sustained headroom. Pure logic (no graphics): unit-tested.
#pragma once

#include <algorithm>

namespace sky {

class FrameGovernor {
public:
    static constexpr int kMaxDetailDrop = 3;

    double scale() const { return scale_; }       // 3D scene resolution factor (0.5 .. 1)
    int detailDrop() const { return detail_; }    // 0 full detail .. kMaxDetailDrop

    void reset() { scale_ = 1.0; detail_ = 0; upBlockedUntil_ = 0; lastUp_ = -100; lastDetail_ = -100; }
    /// Forget the resolution / detail chosen (e.g. a new preset) but keep the timing state.
    void resetLevels() { scale_ = 1.0; detail_ = 0; }

    /// Called about once a second with the mean frame time of that second.
    void step(double now, double meanMs, double budgetMs) {
        if (budgetMs <= 0) return;
        if (meanMs > budgetMs * 1.12) {
            if (scale_ > 0.55) {
                scale_ = std::max(0.5, scale_ - 0.1);
                upBlockedUntil_ = now + ((now - lastUp_) < 6.0 ? 60.0 : 8.0);
            } else if (detail_ < kMaxDetailDrop && now - lastDetail_ >= 3.0) {
                // the resolution floor is reached and it is still too slow: shed scene detail
                ++detail_;
                lastDetail_ = now;
                upBlockedUntil_ = now + 10.0;
            }
        } else if (now > upBlockedUntil_) {
            if (detail_ > 0 && meanMs < budgetMs * 0.8) {
                // clear headroom (detail costs more than one resolution step): bring detail back first
                --detail_;
                lastDetail_ = now;
                upBlockedUntil_ = now + 6.0;
            } else if (detail_ == 0 && meanMs < budgetMs * 1.02 && scale_ < 1.0) {
                scale_ = std::min(1.0, scale_ + 0.1);
                lastUp_ = now;
                upBlockedUntil_ = now + 4.0;
            }
        }
    }

private:
    double scale_ = 1.0;
    int detail_ = 0;
    double upBlockedUntil_ = 0, lastUp_ = -100, lastDetail_ = -100;
};

}  // namespace sky
