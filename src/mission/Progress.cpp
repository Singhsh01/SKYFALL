#include "Progress.hpp"

#include "Mission.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace sky {

namespace {
// v3 records (the twenty combat operations): "done,best,attempts,seconds". The operations are new
// sorties, so records of the earlier academies (keys academy2_l.. and academy_l..) are not carried
// over; they stay in the settings file untouched for older builds.
std::string academyKey(int level, int aircraft) {
    char b[48];
    std::snprintf(b, sizeof b, "academy3_op%02d_a%d", level, aircraft);
    return b;
}
std::string campKey(const std::string& loc) { return "campaign_" + loc; }
}  // namespace

AcademyRecord academyRecord(const Settings& s, int level, int aircraft) {
    AcademyRecord r;
    auto it = s.extra.find(academyKey(level, aircraft));
    if (it != s.extra.end()) {
        int done = 0;
        std::sscanf(it->second.c_str(), "%d,%lf,%d,%lf", &done, &r.best, &r.attempts, &r.seconds);
        r.done = done != 0;
        return r;
    }
    return r;
}

static void store(Settings& s, int level, int aircraft, const AcademyRecord& r) {
    char b[96];
    std::snprintf(b, sizeof b, "%d,%.1f,%d,%.0f", r.done ? 1 : 0, r.best, r.attempts, r.seconds);
    s.extra[academyKey(level, aircraft)] = b;
}

void recordAcademyAttempt(Settings& s, int level, int aircraft) {
    AcademyRecord r = academyRecord(s, level, aircraft);
    ++r.attempts;
    store(s, level, aircraft, r);
}

bool recordAcademyResult(Settings& s, int level, int aircraft, const ScoreCard& card) {
    AcademyRecord r = academyRecord(s, level, aircraft);
    bool best = card.total > r.best + 1e-6;
    if (best) {
        r.best = card.total;
        r.seconds = card.flightSeconds;
    }
    r.done = true;
    store(s, level, aircraft, r);
    return best;
}

bool academyCompleted(const Settings& s, int level) {
    for (int a = 0; a < static_cast<int>(AircraftId::Count); ++a)
        if (academyRecord(s, level, a).done) return true;
    return false;
}

bool campaignCompletedAt(const Settings& s, const std::string& loc, int n) {
    if (loc.empty() || loc == "desert") return s.missionCompleted(n);
    auto it = s.extra.find(campKey(loc));
    if (it == s.extra.end() || n < 1 || n > static_cast<int>(it->second.size())) return false;
    return it->second[n - 1] == '1';
}

void markCampaignCompletedAt(Settings& s, const std::string& loc, int n) {
    if (loc.empty() || loc == "desert") { s.markCompleted(n); return; }
    std::string& v = s.extra[campKey(loc)];
    if (v.size() < static_cast<size_t>(kCampaignMissions)) v.resize(kCampaignMissions, '0');
    if (n >= 1 && n <= kCampaignMissions) v[n - 1] = '1';
}

int campaignCountAt(const Settings& s, const std::string& loc) {
    int c = 0;
    for (int n = 1; n <= kCampaignMissions; ++n) c += campaignCompletedAt(s, loc, n) ? 1 : 0;
    return c;
}

}  // namespace sky
