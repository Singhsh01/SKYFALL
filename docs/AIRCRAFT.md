# Aircraft data & provenance

All eleven slots exist in `src/aircraft/AircraftProfile.cpp` (ten fighters and one transport). Each profile holds mass, fuel,
reference wing area, span, length, lift/drag coefficients, dry and afterburner thrust, spool time,
fuel consumption, roll/pitch/yaw rate limits, G limit, AoA limiter, rotation speed, camera anchors
and a loadout.

| Slot | In this build | Mesh |
|---|---|---|
| F-22 Raptor | playable | original procedural "stealth twin-tail" silhouette |
| F-35 Lightning II | playable | original "stealth single-engine" |
| Dassault Rafale | playable | original "delta-canard" |
| Su-57 Felon | playable, AI opponent | stealth twin-tail family (camouflaged) |
| Su-35 | playable, AI opponent | original "Flanker-family" with nacelles |
| MiG-29 | playable, AI opponent | original "Fulcrum" |
| Su-30 | playable, AI opponent | Flanker-family |
| F-16 Fighting Falcon | playable | original "single-tail viper" |
| F/A-18E Super Hornet | playable (carrier-capable), AI opponent | original "Hornet": twin canted tails, LEX, square intakes |
| Eurofighter Typhoon | playable, AI aggressor | original "Typhoon": delta wing, canards, chin intake, single fin |
| C-17 Globemaster III | Free Flight only (no afterburner, no weapons); escorted asset in the Academy | original "Transport": high wing, four turbofans, T-tail, gear sponsons, animated cargo ramp |

**Sources and honesty**

* Mass, wing area, span, length, engine thrust class: rounded figures from public
  manufacturer/air-force fact sheets and standard references. They are approximate and may
  differ by variant.
* `C_L0`, `C_Lα`, `C_Lmax`, stall AoA, `C_D0`, induced drag factor, rate limits, response time
  constants and AoA limits: **tuned illustrative values** chosen for a credible feel. They are not
  derived from — and do not claim to match — non-public performance data.
* Nothing in SKYFALL is flight-certified or suitable for training real pilots.

All ten fighters are playable with the arcade handling model (roll and pitch rates from each profile;
a full-afterburner thrust/weight floor of 1.25 so every jet can climb away). In Assisted /
Simulation handling the published thrust figures are used unchanged. A slot counts as *fully
validated* once it has (1) a properly licensed or original detailed
model, (2) a tuned profile with documented sources, and (3) passing takeoff/turn/stall tests.

**The C-17** is a heavy transport: no afterburner (the throttle's full travel is military power),
arcade roll and pitch rates scaled down, a high maximum lift coefficient and a 70 m/s approach.
It is offered in Free Flight and the hangar; the Academy and the campaign use the ten fighters
(the Academy's C-17 is flown by the game as the asset you escort).
