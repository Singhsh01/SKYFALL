# Flight physics

Units are SI. World frame: right-handed, +Y up. Body frame: +X nose, +Y canopy, +Z right wing.
Quaternions rotate body vectors into the world.

## What is computed every 1/120 s

| Quantity | Model | Status |
|---|---|---|
| Air density, temperature, speed of sound | ISA (ISO 2533) troposphere + lower stratosphere | validated (unit test vs. standard values) |
| Angle of attack α, sideslip β | from body-frame velocity: `α = atan2(-v_y, v_x)`, `β = asin(v_z/V)` | exact |
| Lift | `L = ½ ρ V² S C_L(α)`, perpendicular to velocity in the body x–y plane | equation exact, coefficients illustrative |
| `C_L(α)` | `C_L0 + C_Lα·α` capped at `C_Lmax`, decaying after the stall towards flat-plate `1.1·sin 2α` | heuristic |
| Drag | `D = ½ ρ V² S C_D`, `C_D = C_D0 + k·C_L² + gear + airbrake + post-stall term` | heuristic |
| Side force | `−0.9·β` coefficient | heuristic |
| Thrust | spooling core (first-order lag), dry 0–100 % + afterburner light-up; × `(ρ/ρ0)^0.7 (1 + 0.25 M)` lapse | heuristic |
| Fuel | TSFC × thrust (dry/AB blend), flameout at zero | approximate |
| Gravity | `m g` with g = 9.80665 | exact |
| Ground | gear contact height, normal reaction, rolling μ 0.022, wheel brakes μ 0.5, parking brake μ 0.6, lateral tyre friction, nosewheel steering | heuristic |
| Touchdown | crash if sink > 7 m/s, bank > 15°, gear up, nose-first, tail strike, slope > ~18° off-runway, water | game rules |
| Airframe collision | nose, wingtips, tail, belly sampled against terrain each step | simplified |

## Rotation: assisted fly-by-wire (documented approximation)

Rotational dynamics are **not** a full aerodynamic-moment + inertia-tensor model yet. The
aircraft's body rates follow a commanded rate through a first-order response whose time constant
grows as dynamic pressure falls (less control authority when slow):

* **Pitch — G command.** Stick commands a load factor `n_cmd` (neutral stick ≈ hold the flight
  path, with turn compensation below 70° bank in *Assisted*). The law converts `n_cmd` into an AoA
  target `α_cmd = (n_cmd W / (q̄ S) − C_L0)/C_Lα`, clamps it to the AoA limiter (24°), and commands
  `q = (n_ach g − g cos γ cos φ)/V + 4(α_cmd − α)` where `n_ach` is what the wing can actually
  deliver at `α_cmd`. This is why the jet cannot pull more than ~9 g and does not depart in
  *Assisted*, while a slow aircraft runs out of lift rather than stalling.
* **Roll — rate command**, scaled by dynamic pressure.
* **Yaw — rate command** plus a sideslip-cancelling term (strong in *Assisted*, weak in
  *Simulation*).
* **Simulation handling** removes the AoA limiter and turn compensation; beyond the stall AoA the
  aircraft loses authority, drops its nose and a wing.

The translational motion is fully force-based, so turns, energy bleed, climbs, stalls and
take-off distance emerge from the forces above (unit-tested: lift-off ≈ 0.8 km, ~20°/s sustained
turn at 9 g, level flight held with neutral stick).

## Integrator

Semi-implicit (symplectic) Euler: `v += a·dt; x += v·dt;` attitude integrated with the exact
axis–angle quaternion exponential and renormalised. At 120 Hz this is stable for the stiff
ground-contact and rate-response terms, cheap, and **deterministic** (no wall-clock or random
input): two identical runs produce bit-identical states (test
`simulation_is_deterministic`). RK4 would buy accuracy we cannot validate without real data.

## Next physics milestone

Replace the rate-command lump with 6-DOF moments: `M = q̄ S c̄ (C_m0 + C_mα α + C_mq q c̄/2V + C_mδe δe)`,
equivalent roll/yaw derivatives, a diagonal inertia tensor per aircraft, and keep the FBW as a
controller *on top of* the moment model. Add wind, turbulence and carrier deck motion.
