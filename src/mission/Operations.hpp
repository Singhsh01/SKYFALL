// LOCATION OPERATIONS — a ten-mission campaign generated for every world location.
//
// The original 61-mission campaign is designed for the Skyfall Desert and stays unchanged. Each
// location (the desert included) also gets an "Operations" campaign whose missions are built from
// the terrain itself: patrol routes follow valleys, radars sit on real peaks, SAM sites guard the
// passes on the valley paths, convoys drive in the basins, trains run on the generated railway,
// coastal maps get naval targets on open water, enemy waves come in low over the ridges, and the
// final mission ends with a landing at the location's airfield. Mission texts are fixed templates
// (with the location name), so every spoken line can be pre-recorded.
#pragma once

#include "Campaign.hpp"
#include "Voice.hpp"
#include "../sim/Locations.hpp"
#include "../sim/WorldLayout.hpp"

#include <string>
#include <vector>

namespace sky {

constexpr int kOperationMissions = 10;

struct OperationTemplate {
    std::string title;     // "Valley Patrol"
    std::string summary;   // one line for the menu
};
const std::vector<OperationTemplate>& operationTemplates();

/// Mission k (1..10) of a location's Operations campaign, built from its terrain. Deterministic.
CampaignMissionDef buildOperation(int k, const Terrain& terrain, const WorldLayout& layout);
/// Spoken briefing (static text: template + location name).
Briefing operationBriefing(const std::string& locationId, int k);
/// Every line the Operations campaigns can say (voice pack), for all locations.
std::vector<VoiceLine> operationVoiceLines();
/// Whether the location has open sea (coastal missions use ships instead of an airbase strike).
bool locationIsCoastal(const std::string& locationId);

}  // namespace sky
