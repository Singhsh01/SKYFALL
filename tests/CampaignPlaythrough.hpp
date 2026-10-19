// Shared by the test programs: drives a campaign mission through its objectives with scripted
// shortcuts (teleports, debug kills) - proves the objective logic, not the flying.
#pragma once

#include "aircraft/AircraftProfile.hpp"
#include "combat/Combat.hpp"
#include "mission/Campaign.hpp"
#include "mission/Mission.hpp"
#include "sim/FlightModel.hpp"

#include <cmath>

namespace sky {
struct CampaignPlaythrough {
    const CampaignMissionDef& def;
    const Terrain& T;
    Battle battle;
    Mission mission;
    CampaignDirector director;
    CampaignPlaythrough(const CampaignMissionDef& d, const Terrain& terrain_, const WorldLayout* layout_)
        : def(d), T(terrain_), battle(T), mission(buildCampaignMission(d)) {
        if (layout_) battle.setLayout(layout_);
        Jet& p = battle.createPlayer(AircraftId::F22);
        if (d.carrier) p.fm->resetOnCatapult();
        else p.fm->resetAirborne({d.startX, d.startAlt, d.startZ}, campaignStartHeading(d), d.startSpeed, 0.8);
        if (d.startFuel >= 0) p.fm->mutableState().fuelKg = d.startFuel * p.fm->profile().fuelMassKg;
        director.begin(d, battle);
        battle.setAiEnabled(false);
        director.update(mission, battle);
    }
    void step(const ControlInputs& in) {
        battle.update(FlightModel::kDt, in, PlayerCommands{});
        MissionContext ctx{&battle.player().fm->state(), &battle.player().fm->profile(), &T, 0, &battle};
        mission.update(ctx, FlightModel::kDt);
        director.update(mission, battle);
    }
    bool run(double maxSeconds) {
        const int maxSteps = static_cast<int>(maxSeconds * 120);
        for (int i = 0; i < maxSteps && mission.status() == MissionStatus::Running; ++i) {
            const ObjectiveDef& o = def.objectives[mission.stepIndex()];
            FlightModel& fm = *battle.player().fm;
            ControlInputs in;
            in.throttleRate = fm.state().throttle < 0.95 ? 1.0 : 0.0;
            switch (o.kind) {
                case ObjectiveKind::Waypoint: {
                    double y = T.surfaceHeight(o.x, o.z) + (o.maxAgl > 0 ? o.maxAgl * 0.5 : 800.0);
                    if (std::hypot(fm.state().pos.x - o.x, fm.state().pos.z - o.z) > o.radius * 0.5)
                        fm.resetAirborne({o.x, y, o.z}, 0.0, 220, 0.8);
                    break;
                }
                case ObjectiveKind::DestroyGroup:
                    for (int gi = 0; gi < static_cast<int>(battle.ground().size()); ++gi)
                        if (battle.ground()[gi].group == o.group && battle.ground()[gi].alive) { battle.debugDestroyGround(gi); break; }
                    break;
                case ObjectiveKind::DestroyAir:
                    for (auto& j : battle.jets())
                        if (j->team == Team::Red && j->alive) { battle.debugKillJet(j->id); break; }
                    break;
                case ObjectiveKind::PullG: in.roll = toEuler(fm.state().q).bank < 75 * kDeg ? 1.0 : 0.0; in.pitch = 1.0; break;
                case ObjectiveKind::Loop: in.pitch = 1.0; break;
                case ObjectiveKind::AileronRoll: in.roll = 1.0; in.pitch = 0.1; break;
                case ObjectiveKind::Touchdown:
                    if (!fm.state().onGround) fm.resetOnRunway(false);
                    break;
                case ObjectiveKind::Stop: in.throttleRate = -1.0; in.brake = 1.0; break;
                case ObjectiveKind::FullPower: in.throttleRate = 1.0; break;
                case ObjectiveKind::Launch:
                    in.throttleRate = 1.0;
                    if (fm.state().parkingBrake && fm.state().throttle >= FlightModel::kAbDetent) fm.setParkingBrake(false);
                    break;
                case ObjectiveKind::GearUp:
                    if (fm.state().gearDown && !fm.state().onGround && fm.state().agl > 40) fm.toggleGear();
                    break;
                case ObjectiveKind::Climb: in.pitch = toEuler(fm.state().q).pitch < 14 * kDeg ? 0.3 : 0.0; break;
                case ObjectiveKind::Rendezvous: {
                    const Tanker& tk = battle.tanker();
                    Vec3 behind = tk.pos - Vec3{std::cos(tk.heading), 0, std::sin(tk.heading)} * 300.0 - Vec3{0, 30, 0};
                    if (length(fm.state().pos - tk.pos) > o.radius * 0.5) {
                        double fuel = fm.state().fuelKg;   // a teleport must not refill the tanks
                        fm.resetAirborne(behind, tk.heading, tk.speed, 0.6);
                        fm.mutableState().fuelKg = fuel;
                    }
                    break;
                }
                case ObjectiveKind::Refuel: {
                    // fly formation on the basket: probe (8 m ahead, 1 m up) on the drogue, tanker speed
                    const Tanker& tk = battle.tanker();
                    FlightState& st = fm.mutableState();
                    Quat q = Quat::fromEuler(tk.heading, 0.0, 0.0);
                    st.q = q;
                    st.pos = tk.basket() - q.forward() * 8.0 - q.up() * 1.0 + tk.vel * FlightModel::kDt;
                    st.vel = tk.vel;
                    in.throttleRate = 0.0;
                    break;
                }
            }
            step(in);
            if (fm.state().crashed) return false;
        }
        return mission.status() == MissionStatus::Complete;
    }
};

}  // namespace sky
