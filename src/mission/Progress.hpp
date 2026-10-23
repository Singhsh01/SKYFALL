// Player progress for the Training Academy (completion, best score, attempts and time per level
// and aircraft) and the campaign on each world location (completed missions per location).
// Stored in the settings file's free-form key/value section, so older builds keep it intact.
#pragma once

#include "../app/Settings.hpp"
#include "Training.hpp"

#include <string>

namespace sky {

struct AcademyRecord {
    bool done = false;      // finished the level at least once (completion status)
    double best = 0;        // best total score
    int attempts = 0;
    double seconds = 0;     // flight time of the best run
};
/// The record of a level for one aircraft. Records from the ten-level academy are carried over
/// to the level that grew out of each of them (see Progress.cpp).
AcademyRecord academyRecord(const Settings& s, int level, int aircraft);
/// Records a finished run; returns true if it is a new best for this level and aircraft.
bool recordAcademyResult(Settings& s, int level, int aircraft, const ScoreCard& card);
/// Counts an attempt (started run) without a result.
void recordAcademyAttempt(Settings& s, int level, int aircraft);
/// Finished in any aircraft.
bool academyCompleted(const Settings& s, int level);
/// Every level is open: there are no locks, medals or rewards.
inline bool academyUnlocked(const Settings&, int level) { return level >= 1 && level <= kTrainingLevels; }

/// Campaign missions completed on a world location (the Skyfall Desert keeps the original
/// campaign_done record).
bool campaignCompletedAt(const Settings& s, const std::string& locationId, int n);
void markCampaignCompletedAt(Settings& s, const std::string& locationId, int n);
int campaignCountAt(const Settings& s, const std::string& locationId);

}  // namespace sky
