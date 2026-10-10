// Missile / target picture-in-picture camera, hit cam and crash replay (Game member functions).
#include "Game.hpp"

#include "../render/Convert.hpp"
#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace sky {

namespace {
double fovFor(double dist, double size, double lo, double hi) {
    return std::clamp(2.0 * std::atan(size / std::max(dist, 1.0)) / kDeg, lo, hi);
}
}  // namespace

// ------------------------------------------------------------------------------------------
// Picture in picture
// ------------------------------------------------------------------------------------------
void Game::updatePip(double dt) {
    const Jet& p = battle_.player();
    const FlightState& ps = p.fm->state();
    pip_.age += dt;
    pip_.flash = std::max(0.0, pip_.flash - dt * 2.5);

    auto targetPos = [&](int tj, int tg, Vec3& out) {
        if (tj >= 0) {
            const Jet* t = battle_.jetById(tj);
            if (t && t->alive) { out = t->fm->state().pos; return true; }
        } else if (tg >= 0 && tg < static_cast<int>(battle_.ground().size()) && battle_.ground()[tg].alive) {
            out = battle_.ground()[tg].pos + Vec3{0, 2.5, 0};
            return true;
        }
        return false;
    };

    if (pip_.mode == PipMode::Missile) {
        const Missile* m = nullptr;
        for (const Missile& mm : battle_.missiles()) if (mm.id == pip_.missileId) m = &mm;
        if (m) {
            pip_.lastPos = m->pos;
            pip_.lastDir = normalize(m->vel);
            pip_.targetJet = m->targetJet;
            pip_.targetGround = m->targetGround;
            Vec3 tp;
            pip_.haveTarget = targetPos(m->targetJet, m->targetGround, tp);
            if (pip_.haveTarget) { pip_.lastTarget = tp; pip_.lastDist = length(tp - m->pos); }
        } else {
            // the missile is gone: classify the result and hold the camera on the impact point
            bool killed = time_ - lastKillTime_ < 0.35 && length(lastKillPos_ - pip_.lastPos) < 120.0;
            bool hit = time_ - lastHitTime_ < 0.35 && length(lastHitPos_ - pip_.lastPos) < 120.0;
            pip_.result = killed ? 2 : (hit ? 1 : 0);
            pip_.missDist = pip_.haveTarget ? pip_.lastDist : 0.0;
            pip_.mode = PipMode::Hit;
            pip_.timer = 2.8;
            pip_.flash = pip_.result > 0 ? 1.0 : 0.0;
            Vec3 back = pip_.lastDir * -42.0 + Vec3{0, 9.0, 0};
            pip_.holdPos = pip_.lastPos + back;
            double gy = terrain_.surfaceHeight(pip_.holdPos.x, pip_.holdPos.z) + 6.0;
            pip_.holdPos.y = std::max(pip_.holdPos.y, gy);
            pip_.holdLook = pip_.lastPos;
        }
    } else if (pip_.mode == PipMode::Hit) {
        pip_.timer -= dt;
        if (pip_.timer <= 0) pip_.mode = PipMode::Off;
    }
    if (pip_.mode == PipMode::Off || pip_.mode == PipMode::Target) {
        bool locking = p.alive && p.lockTarget != -1 && p.lockProgress > 0.02;
        if (locking) {
            int tj = p.lockTarget >= 0 ? p.lockTarget : -1;
            int tg = p.lockTarget <= -1000 ? -1000 - p.lockTarget : -1;
            Vec3 tp;
            if (targetPos(tj, tg, tp)) {
                if (pip_.mode != PipMode::Target) pip_.age = 0;
                pip_.mode = PipMode::Target;
                pip_.targetJet = tj; pip_.targetGround = tg;
                pip_.lastTarget = tp; pip_.haveTarget = true;
                pip_.lastDist = length(tp - ps.pos);
            } else pip_.mode = PipMode::Off;
        } else pip_.mode = PipMode::Off;
    }

    // camera for the active mode
    Camera3D& cam = pip_.cam;
    cam.up = {0, 1, 0};
    cam.projection = CAMERA_PERSPECTIVE;
    switch (pip_.mode) {
        case PipMode::Target: {
            // stabilised narrow-field sensor view from the nose, zoomed so the target fills ~1/4
            Vec3 eye = ps.pos + ps.q.forward() * 9.0 + ps.q.up() * 0.6;
            cam.position = toRl(eye);
            cam.target = toRl(pip_.lastTarget);
            cam.fovy = static_cast<float>(fovFor(length(pip_.lastTarget - eye), pip_.targetJet >= 0 ? 17.0 : 40.0, 0.5, 30.0));
            break;
        }
        case PipMode::Missile: {
            Vec3 d = pip_.lastDir;
            Vec3 eye = pip_.lastPos - d * 11.0 + Vec3{0, 2.2, 0};
            Vec3 look = pip_.lastPos + d * 150.0;
            if (pip_.haveTarget) look = lerp(look, pip_.lastTarget, 0.55);
            cam.position = toRl(eye);
            cam.target = toRl(look);
            cam.fovy = 48.0f;
            break;
        }
        case PipMode::Hit: {
            double t = 1.0 - pip_.timer / 2.8;
            Vec3 eye = lerp(pip_.holdPos, pip_.holdLook + (pip_.holdPos - pip_.holdLook) * 0.7, t);
            cam.position = toRl(eye);
            cam.target = toRl(pip_.holdLook);
            cam.fovy = static_cast<float>(46.0 - 10.0 * t);
            break;
        }
        default: break;
    }
}

// ------------------------------------------------------------------------------------------
// Flight recorder
// ------------------------------------------------------------------------------------------
void Game::recordFrame() {
    const Jet& p = battle_.player();
    if (!p.alive) return;
    const FlightState& s = p.fm->state();
    RecFrame f;
    f.t = time_;
    auto fill = [](RecJet& r, const Jet& j) {
        const FlightState& st = j.fm->state();
        r.id = j.id;
        r.type = j.fm->profile().id;
        r.team = j.team == Team::Blue ? 0 : 1;
        r.pos = st.pos;
        r.q = st.q;
        r.gear = static_cast<float>(st.gearPos);
        r.throttle = static_cast<float>(std::clamp((st.rpm - 0.6) / 0.4, 0.0, 1.0));
        r.ab = static_cast<float>(st.abLevel);
    };
    fill(f.player, p);
    for (const auto& jp : battle_.jets()) {
        if (jp->isPlayer || !jp->alive) continue;
        if (length(jp->fm->state().pos - s.pos) > 5000.0) continue;
        RecJet r;
        fill(r, *jp);
        f.others.push_back(r);
    }
    for (const Missile& m : battle_.missiles()) {
        if (length(m.pos - s.pos) > 6000.0) continue;
        f.missiles.push_back({m.id, m.pos, normalize(m.vel), m.motor > 0, m.targetJet == p.id});
    }
    Euler e = toEuler(s.q);
    f.speed = static_cast<float>(s.airspeed);
    f.vs = static_cast<float>(s.vel.y);
    f.agl = static_cast<float>(s.pos.y - terrain_.surfaceHeight(s.pos.x, s.pos.z));
    f.gLoad = static_cast<float>(s.gLoad);
    f.pitchDeg = static_cast<float>(e.pitch / kDeg);
    f.bankDeg = static_cast<float>(e.bank / kDeg);
    f.throttle = static_cast<float>(s.throttle);
    f.gearDown = s.gearDown;
    f.gpws = gpws_;
    f.missileWarning = battle_.playerMissileWarning();
    f.flaresActive = p.flareBurstTimer > 0;
    f.stalled = s.stalled;
    recorder_.offer(f);
}

// ------------------------------------------------------------------------------------------
// Crash / shoot-down replay
// ------------------------------------------------------------------------------------------
void Game::beginReplay() {
    if (recorder_.frames().size() < 20) return;
    replayFrom_ = std::max(recorder_.startTime(), deathTime_ - 5.5);
    replayTo_ = deathTime_ + 1.8;
    replayT_ = replayFrom_;
    replayExploded_ = false;
    replayActive_ = true;
    renderer_.effects().clear();
    // side observer: perpendicular to the final flight path, above its midpoint
    Vec3 pEnd = recorder_.playerAt(deathTime_).pos;
    Vec3 pBefore = recorder_.playerAt(deathTime_ - 1.0).pos;
    Vec3 pStart = recorder_.playerAt(replayFrom_).pos;
    Vec3 d = pEnd - pBefore;
    d.y = 0;
    d = length(d) > 1.0 ? normalize(d) : Vec3{1, 0, 0};
    Vec3 side = normalize(cross(d, Vec3{0, 1, 0}));
    Vec3 mid = (pStart + pEnd) * 0.5;
    double span = length(pEnd - pStart);
    obsPos_ = mid + side * std::max(160.0, span * 0.32) + Vec3{0, 45.0, 0};
    obsPos_.y = std::max(obsPos_.y, terrain_.surfaceHeight(obsPos_.x, obsPos_.z) + 25.0);
    // second angle: a ground observer near the impact, looking back up the path
    obsPos2_ = crashPos_ - d * 140.0 + side * 70.0;
    obsPos2_.y = terrain_.surfaceHeight(obsPos2_.x, obsPos2_.z) + 14.0;
    obsPos2_.y = std::max(obsPos2_.y, crashPos_.y + 8.0);
}

void Game::endReplay() {
    replayActive_ = false;
    renderer_.effects().clear();
    if (mode_ == GameMode::Arena) respawnPlayer();
}

void Game::updateReplay(double dt) {
    const InputState& in = input_.state();
    if (in.confirm || IsKeyPressed(KEY_SPACE) || in.reset) { endReplay(); return; }
    bool slow = replayT_ > deathTime_ - 1.4 && replayT_ < deathTime_ + 0.5;
    double speed = slow ? 0.33 : 1.0;
    double step = dt * speed;
    replayT_ += step;
    if (!replayExploded_ && replayT_ >= deathTime_) {
        replayExploded_ = true;
        bool water = terrain_.isWater(crashPos_.x, crashPos_.z);
        renderer_.effects().spawnExplosion(toRl(crashPos_), 2.8f, !water && lossCause_ == LossCause::Crash, water);
        audio_.play(Sfx::BigExplosion, 1.4f);
    }
    // recorded scene at this instant
    replayJets_.clear();
    replayMissiles_.clear();
    double tt = std::min(replayT_, deathTime_);
    if (replayT_ < deathTime_) {
        RecJet pj = recorder_.playerAt(tt);
        ExtraJet e;
        e.id = pj.type; e.style = profile(pj.type).style; e.team = Team::Blue;
        e.pos = pj.pos; e.q = pj.q; e.gear = pj.gear; e.throttle = pj.throttle; e.ab = pj.ab;
        replayJets_.push_back(e);
        // engine smoke / vapour hint so the path reads in the replay
        if (GetRandomValue(0, 2) == 0)
            renderer_.effects().spawnSmokePuff(toRl(pj.pos - pj.q.forward() * 9.0), {0, 0, 0}, 0.8f, 4.0f, 1.5f, {0.9f, 0.9f, 0.92f}, 0.25f);
    }
    if (const RecFrame* f = recorder_.frameAt(tt)) {
        for (const RecJet& o : f->others) {
            ExtraJet e;
            e.id = o.type; e.style = profile(o.type).style; e.team = o.team == 0 ? Team::Blue : Team::Red;
            e.pos = o.pos; e.q = o.q; e.gear = o.gear; e.throttle = o.throttle; e.ab = o.ab;
            replayJets_.push_back(e);
        }
        for (const RecMissile& m : f->missiles) {
            replayMissiles_.push_back({m.pos, m.dir});
            if (m.motor)
                renderer_.effects().spawnSmokePuff(toRl(m.pos - m.dir * 2.5), {0, 0, 0}, 1.2f, 6.0f, 4.0f, {0.92f, 0.92f, 0.93f}, 0.7f);
        }
    }
    std::vector<const JetModel*> none;
    renderer_.effects().update(step, replayStage_, lighting_, cam_.position, none);

    // camera: side observer, then a low observer near the impact for the final seconds
    Vec3 focus = replayT_ < deathTime_ ? recorder_.playerAt(replayT_).pos : crashPos_;
    bool second = replayT_ > deathTime_ - 1.4;
    Vec3 eye = second ? obsPos2_ : obsPos_;
    cam_.position = toRl(eye);
    cam_.target = toRl(focus);
    cam_.up = {0, 1, 0};
    double dist = length(focus - eye);
    cam_.fovy = static_cast<float>(replayT_ < deathTime_ ? fovFor(dist, 28.0, 7.0, 55.0) : 55.0);
    nearPlane_ = 0.5f;
    if (replayT_ >= replayTo_) endReplay();
}

FrameParams Game::makeReplayFrameParams() {
    FrameParams f = makeFrameParams();
    f.battle = &replayStage_;
    f.groundFrom = &battle_;
    f.hideJetId = -1;
    f.extraJets = &replayJets_;
    f.extraMissiles = &replayMissiles_;
    f.blackout = f.redout = f.damage = 0;
    f.shadowFocus = toRl(recorder_.playerAt(std::min(replayT_, deathTime_)).pos);
    return f;
}

}  // namespace sky
