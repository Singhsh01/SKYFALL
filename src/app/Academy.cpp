// Training Academy: the academy menu (levels, location, aircraft, route preview, previous
// scores) and running a training course (instructor radio, gate markers, checkpoint retries,
// debrief and progress).
#include "Game.hpp"
#include "../mission/Progress.hpp"
#include "../render/Convert.hpp"
#include "../sim/Locations.hpp"
#include "../ui/Font.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace sky {

using ui::Align;

namespace {
const Color kAcc = {150, 255, 205, 255};
const Color kAmber = {255, 176, 60, 255};
const Color kDim = {170, 190, 185, 210};

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
/// Completion mark (a filled green dot).
void doneBadge(float x, float y, float r) {
    DrawCircleV({x, y}, r, Color{120, 230, 160, 255});
    DrawCircleLinesV({x, y}, r, Color{0, 0, 0, 160});
}
std::vector<std::string> wrap(const std::string& s, float size, float maxW) {
    std::vector<std::string> out;
    std::string line, word;
    for (char ch : s + " ") {
        if (ch != ' ') { word += ch; continue; }
        if (!line.empty() && ui::measure(line + " " + word, size, false) > maxW) { out.push_back(line); line = word; }
        else line = line.empty() ? word : line + " " + word;
        word.clear();
    }
    if (!line.empty()) out.push_back(line);
    return out;
}
std::string mmss(double sec) {
    char b[32];
    int t = static_cast<int>(std::lround(sec));
    std::snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
    return b;
}
}  // namespace

// ------------------------------------------------------------------------------------------
// Academy menu
// ------------------------------------------------------------------------------------------
std::string Game::academyLocationFor(int level) const {
    if (!acLocation_.empty() && academyLocationAllowed(acLocation_, level)) return acLocation_;
    return trainingLevel(level).location;
}

bool Game::corridorOn() const {
    auto it = settings_.extra.find("academy_corridor");
    return it != settings_.extra.end() && it->second == "1";
}

void Game::openAcademy() {
    int lv = acLevel_;
    startTitle();
    screen_ = Screen::Academy;
    acLevel_ = std::clamp(lv, 1, kTrainingLevels);
    nav_.focus = acLevel_ - 1;
}

void Game::drawAcademy() {
    const InputState& in = input_.state();
    float w = static_cast<float>(GetScreenWidth()), h = static_cast<float>(GetScreenHeight());
    float s = h / 720.0f;
    DrawRectangleGradientH(0, 0, static_cast<int>(w), static_cast<int>(h), Color{0, 0, 0, 220}, Color{0, 0, 0, 140});
    ui::title("TRAINING ACADEMY", 50 * s, 22 * s, 32 * s);
    const int kLoc = kTrainingLevels, kAc = kLoc + 1, kMode = kLoc + 2, kCorr = kLoc + 3, kLaunch = kLoc + 4, kBack = kLoc + 5;
    nav_.begin(kBack + 1, in.up, in.down, in.confirm, in.left, in.right);
    if (nav_.focus < kTrainingLevels) acLevel_ = nav_.focus + 1;
    const int lv = acLevel_;
    const TrainingLevelDef& L = trainingLevel(lv);
    int done = 0;
    for (int i = 1; i <= kTrainingLevels; ++i) if (academyCompleted(settings_, i)) ++done;
    ui::text(std::to_string(done) + " / " + std::to_string(kTrainingLevels) + " COMPLETED", w - 50 * s, 34 * s, 15 * s, kAcc, Align::Right);

    // ---- levels: two columns of ten, all open ----
    float x = 50 * s, y0 = 72 * s, bw = 500 * s, bh = 30 * s;
    float colW = (bw - 8 * s) / 2;
    float y = y0;
    for (int i = 0; i < kTrainingLevels; ++i) {
        const TrainingLevelDef& d = trainingLevel(i + 1);
        float bx = x + (i / 10) * (colW + 8 * s), by = y0 + (i % 10) * (bh + 3 * s);
        char label[96];
        std::snprintf(label, sizeof label, "%02d  %s", d.number, upper(d.title).c_str());
        if (ui::button(nav_, i, {bx, by, colW, bh}, label, "", true)) {
            audio_.play(Sfx::Click);
            nav_.focus = kLaunch;
            return;
        }
        if (academyCompleted(settings_, i + 1)) doneBadge(bx + colW - 12 * s, by + bh / 2, 5 * s);
        y = std::max(y, by + bh + 3 * s);
    }
    // ---- options ----
    y += 6 * s;
    float oh = 34 * s;
    {
        std::vector<std::string> ids, names;
        for (const LocationDef& l : locations())
            if (academyLocationAllowed(l.id, lv)) { ids.push_back(l.id); names.push_back(upper(l.name) + (locationAvailable(l.id) ? "" : "  (NOT DOWNLOADED)")); }
        std::string cur = academyLocationFor(lv);
        int li = 0;
        for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == cur) li = static_cast<int>(i);
        if (ui::cycle(nav_, kLoc, {x, y, bw, oh}, "Location", li, names)) {
            acLocation_ = ids[li];
            audio_.play(Sfx::Click, 0.5f);
            if (!locationAvailable(acLocation_)) academyMsg_ = upper(findLocation(acLocation_)->name) + " is not downloaded yet: get it in WORLD LOCATIONS.";
            else if (acLocation_ != terrain_.info().id) { switchTo_ = acLocation_; academyAfterSwitch_ = true; academyMsg_.clear(); }
        }
        y += oh + 4 * s;
    }
    {
        int ac = combatAircraft(settings_.aircraft);
        std::vector<std::string> names;
        for (int a = 0; a < kFighterCount; ++a) names.push_back(allProfiles()[a].name);   // fighters only
        if (ui::cycle(nav_, kAc, {x, y, bw, oh}, "Aircraft", ac, names)) { settings_.aircraft = ac; saveSettings(); audio_.play(Sfx::Click, 0.5f); }
        y += oh + 4 * s;
    }
    {
        int md = settings_.handling;
        if (ui::cycle(nav_, kMode, {x, y, bw, oh}, "Handling", md, {"ARCADE", "ASSISTED", "SIMULATION"})) { settings_.handling = md; saveSettings(); audio_.play(Sfx::Click, 0.5f); }
        y += oh + 4 * s;
    }
    {
        bool corr = corridorOn();
        if (ui::toggle(nav_, kCorr, {x, y, bw, oh}, "Corridor markers to the waypoints", corr)) {
            settings_.extra["academy_corridor"] = corr ? "1" : "0";
            saveSettings();
            audio_.play(Sfx::Click, 0.5f);
        }
        y += oh + 8 * s;
    }
    std::string loc = academyLocationFor(lv);
    const LocationDef* ld = findLocation(loc);
    bool avail = locationAvailable(loc);
    std::string launchSub = !avail ? "Download " + (ld ? ld->name : loc) + " first (WORLD LOCATIONS)"
                                   : std::string(allProfiles()[combatAircraft(settings_.aircraft)].name) + "  -  " + (ld ? ld->name : loc);
    if (ui::button(nav_, kLaunch, {x, y, bw * 0.62f, 50 * s}, "LAUNCH", launchSub, avail)) {
        audio_.play(Sfx::Click);
        startTraining(lv);
        return;
    }
    if (ui::button(nav_, kBack, {x + bw * 0.62f + 8 * s, y, bw * 0.38f - 8 * s, 50 * s}, "BACK") || in.back) { startTitle(); nav_.focus = 1; return; }

    // ---- details of the focused level ----
    float px = x + bw + 28 * s, pw = std::min(w - px - 40 * s, 640 * s);
    Rectangle r = {px, 72 * s, pw, h - 112 * s};
    ui::panel(r, Color{0, 0, 0, 150}, Color{150, 255, 205, 50});
    float ty = r.y + 12 * s;
    ui::text("OPERATION " + std::to_string(lv), r.x + 16 * s, ty, 13 * s, kAmber);
    std::string stars;
    for (int i = 0; i < 5; ++i) stars += i < L.difficulty ? '*' : '.';
    ui::text("DIFFICULTY " + stars, r.x + r.width - 16 * s, ty, 13 * s, kAmber, Align::Right);
    ty += 18 * s;
    ui::text(upper(L.title), r.x + 16 * s, ty, 24 * s, WHITE);
    ty += 34 * s;
    for (const std::string& l : wrap(L.summary, 15 * s, r.width * 0.5f - 24 * s)) { ui::text(l, r.x + 16 * s, ty, 15 * s, Color{225, 232, 232, 235}, Align::Left, false); ty += 20 * s; }
    ty += 6 * s;
    for (const std::string& sk : L.skills) { ui::text("-  " + sk, r.x + 16 * s, ty, 14 * s, kDim, Align::Left, false); ty += 19 * s; }
    if (L.recommendedAircraft >= 0) {
        ui::text(std::string("Best flown in the ") + allProfiles()[L.recommendedAircraft].name, r.x + 16 * s, ty, 14 * s, Color{120, 210, 255, 240}, Align::Left, false);
        ty += 19 * s;
    }
    // previous scores
    ty += 10 * s;
    ui::text("YOUR RECORD", r.x + 16 * s, ty, 12 * s, kAmber);
    ty += 18 * s;
    AcademyRecord rec = academyRecord(settings_, lv, combatAircraft(settings_.aircraft));
    char b[160];
    if (rec.attempts == 0 && !rec.done) std::snprintf(b, sizeof b, "Not flown in the %s yet.", allProfiles()[combatAircraft(settings_.aircraft)].shortName);
    else if (!rec.done) std::snprintf(b, sizeof b, "%s: %d attempt%s, not finished yet.", allProfiles()[combatAircraft(settings_.aircraft)].shortName, rec.attempts, rec.attempts == 1 ? "" : "s");
    else std::snprintf(b, sizeof b, "%s: COMPLETED, best score %.0f, %d attempt%s, %s", allProfiles()[combatAircraft(settings_.aircraft)].shortName, rec.best, rec.attempts,
                       rec.attempts == 1 ? "" : "s", mmss(rec.seconds).c_str());
    ui::text(b, r.x + 16 * s, ty, 14 * s, WHITE, Align::Left, false);
    ty += 22 * s;
    // completion per aircraft: two rows of four
    for (int a = 0; a < kFighterCount; ++a) {
        float colW2 = (r.width * 0.5f - 24 * s) / 4.0f;
        float mx = r.x + 16 * s + (a % 4) * colW2, my = ty + (a / 4) * 22 * s;
        AcademyRecord ra = academyRecord(settings_, lv, a);
        ui::text(allProfiles()[a].shortName, mx, my, 12 * s, a == combatAircraft(settings_.aircraft) ? WHITE : kDim, Align::Left, false);
        float bx = mx + ui::measure(allProfiles()[a].shortName, 12 * s, false) + 12 * s;
        if (ra.done) doneBadge(bx, my + 7 * s, 5 * s);
        else ui::text("-", bx, my, 12 * s, kDim, Align::Center, false);
    }
    ty += 48 * s;
    // route preview (needs the location loaded)
    float mapS = std::min(r.width * 0.5f - 20 * s, r.height - 70 * s);
    Rectangle mr = {r.x + r.width - mapS - 16 * s, r.y + 74 * s, mapS, mapS};
    ui::text("OPERATION AREA", mr.x, mr.y - 18 * s, 12 * s, kAmber);
    bool here = terrain_.info().id == loc;
    if (here) {
        if (previewLevel_ != lv || previewLoc_ != loc) {
            preview_ = buildCourse(lv, terrain_, layout_);
            previewLevel_ = lv;
            previewLoc_ = loc;
        }
        const Texture2D& mm = renderer_.world().minimap;
        const double half = WorldMeshes::kMinimapHalf, map = Terrain::kHalfSize;
        if (mm.id) {
            float px0 = static_cast<float>((half - map) / (2 * half) * mm.width), pw0 = static_cast<float>(map / half * mm.width);
            DrawTexturePro(mm, {px0, px0, pw0, pw0}, mr, {0, 0}, 0, Color{255, 255, 255, 235});
        } else {
            DrawRectangleRec(mr, Color{30, 40, 40, 255});
        }
        DrawRectangleLinesEx(mr, 1, ui::withAlpha(kAcc, 0.5f));
        auto P = [&](Vec3 p) { return Vector2{mr.x + static_cast<float>((p.x + map) / (2 * map)) * mr.width, mr.y + static_cast<float>((p.z + map) / (2 * map)) * mr.height}; };
        if (!preview_.error.empty()) {
            ui::text(preview_.error, mr.x + 10 * s, mr.y + mr.height / 2, 13 * s, Color{255, 170, 120, 240}, Align::Left, false);
        } else {
            const Course& c = preview_;
            for (const Section& sec : c.sections) {
                bool landing = sec.kind == SectionKind::Landing || sec.kind == SectionKind::CarrierLanding;
                int b0 = landing ? sec.gateStart : sec.gateBegin;
                Color col = sec.style == RouteStyle::Obstacles ? Color{120, 220, 255, 255} : landing ? kAmber
                          : sec.style == RouteStyle::LowLevel || sec.style == RouteStyle::Precision ? Color{255, 140, 120, 255} : kAcc;
                for (int i = b0; i + 1 < sec.gateEnd; ++i) DrawLineEx(P(c.gates[i].pos), P(c.gates[i + 1].pos), 2.0f * s, col);
                for (int i = b0; i < sec.gateEnd; ++i) DrawCircleV(P(c.gates[i].pos), 1.6f * s, col);
            }
            // section joins
            Vec3 prev = c.startPos;
            for (const Section& sec : c.sections) {
                bool landing = sec.kind == SectionKind::Landing || sec.kind == SectionKind::CarrierLanding;
                int b0 = landing ? sec.gateStart : sec.gateBegin;
                if (b0 < sec.gateEnd) { DrawLineEx(P(prev), P(c.gates[b0].pos), 1.0f * s, Color{255, 255, 255, 110}); prev = c.gates[sec.gateEnd - 1].pos; }
            }
            for (const Obstacle& o : c.obstacles) DrawCircleV(P(o.pos), 2.4f * s, Color{255, 255, 255, 230});
            // protected assets: the C-17's path, the convoy's road, the rescue route, the base / fleet / troops
            const Color assetCol = {120, 200, 255, 255};
            for (const Asset& a : c.assets) {
                for (size_t k = 1; k < a.path.size(); ++k) DrawLineEx(P(a.path[k - 1]), P(a.path[k]), 1.6f * s, ui::withAlpha(assetCol, 0.85f));
                Vec3 at = a.path.empty() ? a.area : (a.kind == AssetKind::Transport ? a.path.front() : a.path.front());
                if (a.kind == AssetKind::Base || a.kind == AssetKind::Fleet || a.kind == AssetKind::Troops) DrawCircleLinesV(P(a.area), 6.0f * s, assetCol);
                ui::text(a.kind == AssetKind::Transport ? "C-17" : a.name, P(at).x + 7 * s, P(at).y + 3 * s, 10 * s, assetCol);
                if (a.dropS > 0) { DrawCircleV(P(a.area), 3.5f * s, assetCol); ui::text("DROP", P(a.area).x + 6 * s, P(a.area).y - 6 * s, 10 * s, assetCol); }
            }
            if (c.tanker) DrawCircleLinesV(P(c.tankerCenter), static_cast<float>(c.tankerRadius / (2 * map)) * mr.width, Color{255, 225, 120, 200});
            for (const Vec3& p : c.formationRoute) DrawCircleV(P(p), 1.5f * s, Color{200, 200, 255, 220});
            DrawCircleV(P(c.startPos), 4.5f * s, WHITE);
            ui::text("START", P(c.startPos).x + 7 * s, P(c.startPos).y - 6 * s, 10 * s, WHITE);
            if (c.carrier) { DrawCircleV(P(c.landAim), 4.5f * s, kAmber); ui::text("CARRIER", P(c.landAim).x + 7 * s, P(c.landAim).y - 6 * s, 10 * s, kAmber); }
            char est[96];
            int waves = 0, wps = 0;
            for (const Section& sec : c.sections) {
                if (sec.kind == SectionKind::Engage) ++waves;
                if (sec.kind == SectionKind::Route) wps += sec.gateEnd - sec.gateBegin;
            }
            std::snprintf(est, sizeof est, "ABOUT %d MIN   %d WAVES   %d WAYPOINTS%s", L.minutes, waves, wps, c.wingmen.empty() ? "" : (c.wingmen.size() > 1 ? "   2 WINGMEN" : "   WINGMAN"));
            ui::text(est, mr.x, mr.y + mr.height + 8 * s, 12 * s, WHITE, Align::Left, false);
        }
    } else {
        DrawRectangleRec(mr, Color{20, 28, 28, 220});
        DrawRectangleLinesEx(mr, 1, ui::withAlpha(kAcc, 0.3f));
        std::string why = !avail ? "Not downloaded" : (ld ? ld->name : loc) + " is not loaded";
        ui::text(why, mr.x + mr.width / 2, mr.y + mr.height / 2 - 18 * s, 15 * s, kDim, Align::Center, false);
        if (avail) ui::text("pick it under Location to see the route", mr.x + mr.width / 2, mr.y + mr.height / 2 + 6 * s, 12 * s, kDim, Align::Center, false);
    }
    if (!academyMsg_.empty()) ui::text(academyMsg_, r.x + 16 * s, r.y + r.height - 26 * s, 13 * s, Color{255, 170, 120, 240}, Align::Left, false);
    else ui::text(L.needsSea ? "This level ends aboard the carrier: Skyfall Desert, Musandam and Rio have open sea." : "All twenty levels are open. Your completions, best scores and times are kept per aircraft.",
                  r.x + 16 * s, r.y + r.height - 26 * s, 12 * s, kDim, Align::Left, false);
}

// ------------------------------------------------------------------------------------------
// Running a course
// ------------------------------------------------------------------------------------------
void Game::startTraining(int level) {
    level = std::clamp(level, 1, kTrainingLevels);
    acLevel_ = level;
    std::string loc = academyLocationFor(level);
    if (!locationAvailable(loc)) {
        academyMsg_ = loc + " is not downloaded yet: get it in WORLD LOCATIONS.";
        openAcademy();
        return;
    }
    if (terrain_.info().id != loc) { switchTo_ = loc; pendingTraining_ = level; return; }
    Course course = buildCourse(level, terrain_, layout_);
    if (!course.error.empty()) { academyMsg_ = course.error; openAcademy(); return; }
    TraceLog(LOG_INFO, "SKYFALL: training level %d at %s, %.1f min, %zu gates", level, loc.c_str(), course.estimatedSeconds / 60.0, course.gates.size());
    academyMsg_.clear();
    freeFlight_ = false;
    screen_ = Screen::Playing;
    mode_ = GameMode::Training;
    trainingLevel_ = level;
    missionNo_ = 100 + level;
    campaignDef_ = nullptr;
    director_ = CampaignDirector{};
    tod_ = level % 3 == 2 ? TimeOfDay::GoldenHour : (level % 3 == 0 ? TimeOfDay::Noon : TimeOfDay::Morning);
    if (level == 13 || level == 17) tod_ = TimeOfDay::Dusk;   // two combat levels in the evening (lights on)
    lighting_ = Lighting::preset(tod_);
    rain_ = false;
    terrain_.setObstacles(course.obstacles);
    if (course.carrier || course.carrierStart) terrain_.setDeck(course.deck);
    else terrain_.clearDeck();
    const int aircraft = combatAircraft(settings_.aircraft);
    Jet& p = battle_.createPlayer(static_cast<AircraftId>(aircraft));
    battle_.setAiEnabled(true);
    p.fm->resetOnRunway(!course.coldStart);
    FlightState& st = p.fm->mutableState();
    st.pos = {course.startPos.x, course.startPos.y + FlightModel::kGearHeight, course.startPos.z};
    st.q = Quat::fromEuler(course.startHeading, 0, 0);
    if (course.carrierStart) p.fm->resetOnCatapult();
    applySettings();
    training_ = TrainingSession{};
    training_.begin(course, battle_, aircraft, settings_.handling);
    mission_ = training_.buildMission();
    trainingDone_ = trainingNewBest_ = trainingCrashNoted_ = false;
    trainingResult_ = ScoreCard{};
    recordAcademyAttempt(settings_, level, aircraft);
    saveSettings();
    missionDone_ = false;
    voicedStep_ = 0;
    radioStage_ = 99;
    playerDead_ = false;
    respawnTimer_ = -1;
    renderer_.effects().clear();
    hud_.clear();
    chaseInit_ = false;
    headYaw_ = headPitch_ = 0;
    recorder_.clear();
    pip_ = PipState{};
    replayActive_ = false;
    deathWait_ = -1;
    lossCause_ = LossCause::Crash;
    killerName_.clear();
    camMode_ = gameplayCamera();
    gateMarkers_.clear();
    corridor_.clear();
    // the briefing over the radio, then the aircraft and handling notes; the instructor
    // takes over from there
    radioQueue_.clear();
    for (const std::string& part : trainingLevel(level).briefing) queueRadio(Speaker::Captain, part);
    queueRadio(Speaker::Instructor, aircraftHandlingLine(aircraft));
    queueRadio(Speaker::Instructor, handlingModeLine(settings_.handling));
    radioNextAt_ = time_ + 1.0;
    audio_.play(Sfx::Radio);
}

void Game::retryTraining() {
    if (!mission_ || !training_.canRetry()) { startTraining(trainingLevel_); return; }
    if (replayActive_) { replayActive_ = false; renderer_.effects().clear(); }
    training_.retry(battle_);
    mission_->skipTo(training_.checkpointSection());
    playerDead_ = false;
    respawnTimer_ = -1;
    deathWait_ = -1;
    trainingCrashNoted_ = false;
    lossCause_ = LossCause::Crash;
    killerName_.clear();
    recorder_.clear();
    pip_ = PipState{};
    chaseInit_ = false;
    radioQueue_.clear();
    stepper_.accumulator = 0;
    audio_.play(Sfx::Radio, 0.6f);
}

void Game::buildGateMarkers() {
    gateMarkers_.clear();
    corridor_.clear();
    if (!mission_ || mission_->status() != MissionStatus::Running) return;
    const Course& c = training_.course();
    const Gate* g = training_.currentGate();
    if (!g) return;
    const int gi = training_.gateIndex();
    const Section& sec = c.sections[training_.section()];
    const int end = sec.gateEnd;
    for (int i = gi; i < std::min(end, gi + 4); ++i) {
        const Gate& gt = c.gates[i];
        if (gt.cue == GateCue::Glide) continue;   // the approach is flown on the HUD's glide path, no rings
        if (gt.waypoint) {
            if (i > gi + 1) continue;
            GateMarker m;
            m.pos = gt.pos;
            m.dir = gt.dir;
            m.beacon = true;
            m.ground = gt.ground;
            m.radius = static_cast<float>(gt.radius);
            m.color = i == gi ? Color{150, 255, 205, 255} : Color{70, 120, 100, 255};
            m.glow = i == gi ? 1.4f : 0.5f;
            gateMarkers_.push_back(m);
            continue;
        }
        GateMarker m;
        m.pos = gt.pos;
        m.dir = gt.dir;
        m.radius = static_cast<float>(gt.radius);
        bool obstacle = gt.cue == GateCue::Pillars || gt.cue == GateCue::Masts || gt.cue == GateCue::Arch || gt.cue == GateCue::Bridge;
        bool glide = gt.cue == GateCue::Glide;
        Color base = glide ? Color{255, 190, 90, 255} : obstacle ? Color{120, 220, 255, 255} : Color{120, 255, 190, 255};
        float k = i == gi ? 1.0f : (i == gi + 1 ? 0.55f : 0.32f);
        m.color = Color{static_cast<unsigned char>(base.r * k + 20), static_cast<unsigned char>(base.g * k + 20), static_cast<unsigned char>(base.b * k + 20), 255};
        m.glow = i == gi ? 1.5f : 0.6f;
        gateMarkers_.push_back(m);
    }
    if (corridorOn() && battle_.jets().size()) {
        // small beads along the straight line to the next gates (optional aid)
        Vec3 a = battle_.player().fm->state().pos;
        for (int i = gi; i < std::min(end, gi + 3) && corridor_.size() < 48; ++i) {
            Vec3 b = c.gates[i].pos;
            double L = length(b - a);
            int n = std::clamp(static_cast<int>(L / 220.0), 1, 16);
            for (int k = 1; k < n && corridor_.size() < 48; ++k) corridor_.push_back(a + (b - a) * (static_cast<double>(k) / n));
            a = b;
        }
    }
}

void Game::updateTrainingFrame(double /*dt*/) {
    buildGateMarkers();
    // instructor, tower, lead and tanker calls: urgent ones cut in, normal ones queue (short),
    // chatter only into silence
    for (const RadioCall& r : training_.drainRadio()) {
        if (r.priority >= 2) {
            std::deque<std::pair<Speaker, std::string>> keep;
            for (auto& q : radioQueue_) if (q.first == Speaker::Captain) keep.push_back(q);   // the briefing survives
            radioQueue_ = keep;
            double dur = std::max(3.0, r.text.size() / 13.0 + 1.0);
            hud_.radio(speakerName(r.who), r.text, dur);
            if (settings_.voiceBriefings && (!capturing_ || std::getenv("SKYFALL_VOICE"))) speech_.say(r.who, r.text, true);
            audio_.play(Sfx::Radio, 0.5f);
            radioNextAt_ = time_ + dur * 0.8;
        } else if (r.priority == 1) {
            if (radioQueue_.size() < 3) queueRadio(r.who, r.text);
        } else if (radioQueue_.empty() && !speech_.speaking()) {
            queueRadio(r.who, r.text);
        }
    }
    for (const TrainingEvent& e : training_.drainEvents()) {
        switch (e.kind) {
            case TrainingEventKind::GateHit: audio_.play(Sfx::Click, 0.35f); break;
            case TrainingEventKind::Trap: hud_.popup("TRAPPED  -  WIRE " + std::to_string(std::max(1, e.index)), Color{150, 255, 205, 255}); break;
            case TrainingEventKind::Bolter: hud_.popup("BOLTER", Color{255, 176, 60, 255}); break;
            case TrainingEventKind::GoAround: hud_.popup("GO AROUND", Color{255, 176, 60, 255}); break;
            case TrainingEventKind::Touchdown: hud_.popup("TOUCHDOWN", Color{150, 255, 205, 255}); break;
            case TrainingEventKind::SectionDone: audio_.play(Sfx::Radio, 0.3f); break;
            case TrainingEventKind::TargetDestroyed: hud_.popup("TARGET DESTROYED", Color{150, 255, 205, 255}); break;
            default: break;
        }
    }
    if (mission_ && mission_->status() == MissionStatus::Complete && !trainingDone_) {
        trainingDone_ = true;
        trainingResult_ = training_.finalScore();
        trainingNewBest_ = recordAcademyResult(settings_, trainingLevel_, combatAircraft(settings_.aircraft), trainingResult_);
        saveSettings();
        radioQueue_.clear();
        queueRadio(Speaker::Instructor, trainingCourseCompleteLine());
        queueRadio(Speaker::Instructor, trainingRemarkLine(trainingResult_.total));
        radioNextAt_ = time_ + 0.5;
        TraceLog(LOG_INFO, "SKYFALL: training level %d complete, score %.0f, kills %d / %d", trainingLevel_, trainingResult_.total, trainingResult_.kills, trainingResult_.targets);
    }
}

}  // namespace sky
