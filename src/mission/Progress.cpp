#include "Progress.hpp"

#include "Operations.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace sky {

namespace {
std::string academyKey(int level, int aircraft) {
    char b[48];
    std::snprintf(b, sizeof b, "academy_l%02d_a%d", level, aircraft);
    return b;
}
std::string opsKey(const std::string& loc) { return "ops_" + loc; }
}  // namespace

AcademyRecord academyRecord(const Settings& s, int level, int aircraft) {
    AcademyRecord r;
    auto it = s.extra.find(academyKey(level, aircraft));
    if (it == s.extra.end()) return r;
    std::sscanf(it->second.c_str(), "%d,%lf,%d,%lf", &r.medal, &r.best, &r.attempts, &r.seconds);
    r.medal = std::clamp(r.medal, 0, 3);
    return r;
}

static void store(Settings& s, int level, int aircraft, const AcademyRecord& r) {
    char b[96];
    std::snprintf(b, sizeof b, "%d,%.1f,%d,%.0f", r.medal, r.best, r.attempts, r.seconds);
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
    r.medal = std::max(r.medal, static_cast<int>(card.medal));
    store(s, level, aircraft, r);
    return best;
}

int academyBestMedal(const Settings& s, int level) {
    int m = 0;
    for (int a = 0; a < static_cast<int>(AircraftId::Count); ++a) m = std::max(m, academyRecord(s, level, a).medal);
    return m;
}

bool academyUnlocked(const Settings& s, int level) {
    if (level <= 1) return true;
    if (level > kTrainingLevels) return false;
    return academyBestMedal(s, level - 1) >= static_cast<int>(Medal::Bronze);
}

bool operationCompleted(const Settings& s, const std::string& loc, int k) {
    auto it = s.extra.find(opsKey(loc));
    if (it == s.extra.end() || k < 1 || k > static_cast<int>(it->second.size())) return false;
    return it->second[k - 1] == '1';
}

void markOperationCompleted(Settings& s, const std::string& loc, int k) {
    std::string& v = s.extra[opsKey(loc)];
    if (v.size() < static_cast<size_t>(kOperationMissions)) v.resize(kOperationMissions, '0');
    if (k >= 1 && k <= kOperationMissions) v[k - 1] = '1';
}

int operationsCompleted(const Settings& s, const std::string& loc) {
    int n = 0;
    for (int k = 1; k <= kOperationMissions; ++k) n += operationCompleted(s, loc, k) ? 1 : 0;
    return n;
}

bool operationUnlocked(const Settings& s, const std::string& loc, int k) { return k <= 1 || operationCompleted(s, loc, k - 1); }

}  // namespace sky
