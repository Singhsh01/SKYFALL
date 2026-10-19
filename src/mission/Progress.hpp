// Player progress for the Training Academy (best medal / score / attempts per level and aircraft,
// progressive unlocks) and the campaign on each world location (completed missions per location).
// Stored in the settings file's free-form key/value section, so older builds keep it intact.
#pragma once

#include "../app/Settings.hpp"
#include "Training.hpp"

#include <string>

namespace sky {

struct AcademyRecord {
    int medal = 0;          // Medal as int (0 none .. 3 gold)
    double best = 0;        // best total score
    int attempts = 0;
    double seconds = 0;     // flight time of the best run
};
AcademyRecord academyRecord(const Settings& s, int level, int aircraft);
/// Records a finished run; returns true if it is a new best for this level and aircraft.
bool recordAcademyResult(Settings& s, int level, int aircraft, const ScoreCard& card);
/// Counts an attempt (started run) without a result.
void recordAcademyAttempt(Settings& s, int level, int aircraft);
/// Best medal on a level over all aircraft.
int academyBestMedal(const Settings& s, int level);
/// Level 1 is open; level n opens with at least a bronze medal on level n-1 (any aircraft).
bool academyUnlocked(const Settings& s, int level);

/// Campaign missions completed on a world location (the Skyfall Desert keeps the original
/// campaign_done record).
bool campaignCompletedAt(const Settings& s, const std::string& locationId, int n);
void markCampaignCompletedAt(Settings& s, const std::string& locationId, int n);
int campaignCountAt(const Settings& s, const std::string& locationId);

}  // namespace sky
