#include "Replay.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sky {

namespace {
std::string fmt(const char* f, double a) { char b[160]; std::snprintf(b, sizeof b, f, a); return b; }
std::string fmt2(const char* f, double a, double c) { char b[160]; std::snprintf(b, sizeof b, f, a, c); return b; }

Quat slerpQ(const Quat& a, Quat b, double t) {
    double d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    if (d < 0) { b = Quat{-b.w, -b.x, -b.y, -b.z}; d = -d; }
    if (d > 0.9995) {
        return Quat{a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}.normalized();
    }
    double th = std::acos(d), s = std::sin(th);
    double wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return Quat{a.w * wa + b.w * wb, a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb}.normalized();
}
}  // namespace

bool FlightRecorder::offer(const RecFrame& f) {
    if (!frames_.empty() && f.t - frames_.back().t < 1.0 / kRate - 1e-6) return false;
    frames_.push_back(f);
    while (!frames_.empty() && frames_.back().t - frames_.front().t > kWindow) frames_.pop_front();
    return true;
}

const RecFrame* FlightRecorder::frameAt(double t) const {
    if (frames_.empty()) return nullptr;
    const RecFrame* best = &frames_.front();
    for (const RecFrame& f : frames_) {
        if (f.t > t) break;
        best = &f;
    }
    return best;
}

RecJet FlightRecorder::playerAt(double t) const {
    if (frames_.empty()) return RecJet{};
    if (t <= frames_.front().t) return frames_.front().player;
    if (t >= frames_.back().t) return frames_.back().player;
    for (size_t i = 0; i + 1 < frames_.size(); ++i) {
        const RecFrame& a = frames_[i];
        const RecFrame& b = frames_[i + 1];
        if (t >= a.t && t <= b.t) {
            double u = (t - a.t) / std::max(b.t - a.t, 1e-6);
            RecJet r = a.player;
            r.pos = lerp(a.player.pos, b.player.pos, u);
            r.q = slerpQ(a.player.q, b.player.q, u);
            r.throttle = static_cast<float>(lerpd(a.player.throttle, b.player.throttle, u));
            r.ab = static_cast<float>(lerpd(a.player.ab, b.player.ab, u));
            r.gear = static_cast<float>(lerpd(a.player.gear, b.player.gear, u));
            return r;
        }
    }
    return frames_.back().player;
}

double pullOutAltitude(double v, double gammaDeg, double n) {
    if (gammaDeg >= 0 || n <= 1.0) return 0.0;
    double r = v * v / (kG * (n - 1.0));
    return r * (1.0 - std::cos(gammaDeg * kDeg));
}

LossReport analyzeLoss(const FlightRecorder& rec, double deathTime, CrashReason reason, LossCause cause,
                       const std::string& killer) {
    LossReport rep;
    const RecFrame* last = rec.frameAt(deathTime);
    if (!last) {
        rep.title = "AIRCRAFT LOST";
        return rep;
    }
    const RecFrame& f = *last;
    // flight-path angle at impact from the recorded vertical speed
    double gamma = f.speed > 1.0 ? std::asin(clampd(f.vs / f.speed, -1.0, 1.0)) / kDeg : 0.0;

    // how long before the loss did warnings start (continuous run ending at the loss)?
    auto warningLead = [&](bool RecFrame::*flag) {
        double start = -1;
        for (auto it = rec.frames().rbegin(); it != rec.frames().rend(); ++it) {
            if (it->t > deathTime + 1e-6) continue;
            if ((*it).*flag) start = it->t;
            else if (start >= 0) break;
        }
        return start >= 0 ? deathTime - start : -1.0;
    };
    double gpwsLead = warningLead(&RecFrame::gpws);
    double mslLead = warningLead(&RecFrame::missileWarning);
    bool flaresUsed = false;
    double flareLead = -1;
    for (const RecFrame& r : rec.frames())
        if (r.t <= deathTime && r.t >= deathTime - 5.0 && r.flaresActive) {
            if (!flaresUsed) flareLead = deathTime - r.t;
            flaresUsed = true;
        }
    double minAgl = 1e9;
    for (const RecFrame& r : rec.frames())
        if (r.t >= deathTime - 5.0 && r.t <= deathTime - 1.0) minAgl = std::min(minAgl, static_cast<double>(r.agl));

    rep.facts.push_back(fmt2("Speed %.0f km/h, vertical speed %+.0f m/s", f.speed * 3.6, f.vs));
    rep.facts.push_back(fmt2("Pitch %+.0f deg, bank %.0f deg", f.pitchDeg, f.bankDeg));

    if (cause == LossCause::ShotDownMissile || cause == LossCause::ShotDownGun) {
        rep.title = cause == LossCause::ShotDownMissile ? "SHOT DOWN BY MISSILE" : "SHOT DOWN BY CANNON";
        if (!killer.empty()) rep.facts.push_back("Attacker: " + killer);
        if (cause == LossCause::ShotDownMissile) {
            if (mslLead > 0) rep.facts.push_back(fmt("Missile warning sounded %.1f s before the hit", mslLead));
            if (!flaresUsed) rep.advice.push_back("No flares were released. Press X as soon as MISSILE INCOMING appears.");
            else if (flareLead >= 0 && flareLead < 0.8) rep.advice.push_back(fmt("Flares came only %.1f s before impact - release them earlier.", flareLead));
            else rep.advice.push_back("Flares alone were not enough: after releasing them, break hard (bank 90 deg and pull) across the missile's path.");
        } else {
            rep.advice.push_back("A bandit stayed on your six. Break turn (bank hard + pull) and use the afterburner to deny the gun solution.");
        }
        if (f.speed < 140) rep.advice.push_back(fmt("You were slow (%.0f km/h): low energy makes you an easy target.", f.speed * 3.6));
        return rep;
    }

    rep.title = crashReasonText(reason);
    if (rep.title.empty()) rep.title = "AIRCRAFT LOST";
    switch (reason) {
        case CrashReason::HardLanding:
            rep.advice.push_back(fmt("Sink rate %.1f m/s at touchdown (limit 7 m/s). Reduce descent and flare just before the runway.", -f.vs));
            break;
        case CrashReason::BellyLanding:
            rep.advice.push_back("The landing gear was up. Press G on final approach and check GEAR DN on the HUD.");
            break;
        case CrashReason::WingStrike:
            rep.advice.push_back(fmt("Bank %.0f deg at touchdown (limit 15 deg). Level the wings before landing.", std::fabs(f.bankDeg)));
            break;
        case CrashReason::NoseFirst:
            rep.advice.push_back("The nose hit first. Hold a slightly nose-up attitude for touchdown.");
            break;
        case CrashReason::TailStrike:
            rep.advice.push_back("Tail strike: the nose was too high while sinking. Rotate less or keep more speed.");
            break;
        default: {
            if (std::fabs(f.bankDeg) > 70) {
                rep.advice.push_back(fmt("You were rolled %.0f deg: pulling back only drags the nose sideways or down. Roll upright first, then pull.", std::fabs(f.bankDeg)));
            }
            if (gamma < -10) {
                double need = pullOutAltitude(f.speed, gamma, 7.0);
                rep.facts.push_back(fmt2("Dive angle %.0f deg; pulling out at 7 g needed about %.0f m of height", gamma, need));
                if (f.speed > 230) rep.advice.push_back("Too fast in the dive - throttle back (S) and use the air brake (Ctrl) before pointing down.");
            }
            if (gpwsLead > 0) rep.advice.push_back(fmt("PULL UP warning started %.1f s before impact. React to it immediately.", gpwsLead));
            else rep.advice.push_back("Watch the radar altitude (R) on the HUD near terrain; keep a margin above ridgelines.");
            if (minAgl < 120) rep.advice.push_back(fmt("You were only %.0f m above the ground in the last seconds - no margin for error.", minAgl));
            if (f.stalled) rep.advice.push_back("The aircraft was stalled (too slow). Keep above ~300 km/h when manoeuvring low.");
            break;
        }
    }
    if (rep.advice.empty()) rep.advice.push_back("Fly smoother and keep more height in reserve.");
    return rep;
}

}  // namespace sky
