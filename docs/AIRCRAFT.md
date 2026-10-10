# Aircraft data & provenance

All eight slots exist in `src/aircraft/AircraftProfile.cpp`. Each profile holds mass, fuel,
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

**Sources and honesty**

* Mass, wing area, span, length, engine thrust class: rounded figures from public
  manufacturer/air-force fact sheets and standard references. They are approximate and may
  differ by variant.
* `C_L0`, `C_Lα`, `C_Lmax`, stall AoA, `C_D0`, induced drag factor, rate limits, response time
  constants and AoA limits: **tuned illustrative values** chosen for a credible feel. They are not
  derived from — and do not claim to match — non-public performance data.
* Nothing in SKYFALL is flight-certified or suitable for training real pilots.

All eight are playable with the arcade handling model (roll and pitch rates from each profile;
a full-afterburner thrust/weight floor of 1.25 so every jet can climb away). In Assisted /
Simulation handling the published thrust figures are used unchanged. A slot counts as *fully
validated* once it has (1) a properly licensed or original detailed
model, (2) a tuned profile with documented sources, and (3) passing takeoff/turn/stall tests.
