# Minimum enemy-test assets

Bare-minimum asset list to get the Phase 2 zombie working end-to-end
(spawn, wander via nav graph, chase, melee, die, respawn loop). Phase 1
— navigation — needs **no new assets** at all; waypoints are invisible
entities.

See `docs/plan-enemies.md` for the full feature plan.

## Required (Phase 2 — zombie)

### `assets/models/zombie.iqm`

An animated IQM with at least four named animations:

| Logical role   | Used during                            | Example IQM name |
|----------------|----------------------------------------|------------------|
| idle           | IDLE state, not moving                 | `idle`           |
| walk           | CHASE state, locomotion                | `walk`           |
| attack         | ATTACK state (melee swing / bite)      | `bite` / `attack`|
| death          | DEAD state, played once before fade    | `die` / `death`  |

The names inside the IQM are up to the exporter — the `.ent` entry maps
them via `anim_idle=…`, `anim_walk=…`, `anim_attack=…`, `anim_death=…`,
so anything consistent works. Looping is decided by the engine (idle /
walk loop, attack / death play once).

Export pipeline is the same as `assets/models/mrfixit.iqm`: Blender →
IQM exporter (Lee Salzman's Blender addon) with the rig and all clips
in one file.

### Diffuse texture(s) the IQM references

Place **next to the `.iqm` file** in `assets/models/`. `iqmLoadTextures`
derives texture paths from the model's directory + the material name
baked into the IQM (see `mrfixit.iqm` + `Body.tga` / `Head.tga` for the
reference case). `.tga` and `.bmp` both work.

## Optional (skippable for first test)

- **Hurt / flinch animation** on the zombie IQM — plays briefly when
  the player hits it. Phase 2 lists it as optional; without it, the
  zombie just keeps chasing through damage until it dies.
- **Zombie moan + death `.wav`s** in `assets/sounds/` — e.g.
  `zombie_alert.wav`, `zombie_attack.wav`, `zombie_death.wav`. These
  are Phase 5 polish. Register each in `assets.lua` under `sounds =
  { … }` and the engine can trigger via `snd_play("zombie_alert")`.
- **Crosshair / damage-flash textures** — not needed; the damage
  overlay is a flat-colored `uiQuad`.

## Later phases

Each archetype past the zombie follows the same shape: one IQM with the
same four animation roles, plus the archetype-specific additions below.

| Phase | Asset                                   | Notes                             |
|-------|-----------------------------------------|-----------------------------------|
| 3     | `assets/models/soldier.iqm` + textures  | Hitscan — no projectile asset     |
| 3     | `assets/models/orb.obj` + texture       | Generic projectile mesh (shared)  |
| 4     | `assets/models/alien_puppy.iqm` + tex   | Leap attack                       |
| 4     | `assets/models/alien_dog.iqm` + tex     | Fast melee                        |
| 4     | `assets/models/bigalien.iqm` + tex      | Uses the shared orb projectile    |

Each new IQM gets one line in `assets.lua` under `models = { … }` with a
short logical name (e.g. `zombie = "assets/models/zombie.iqm"`), then
`.ent` entries reference it by that name (`iqm=zombie`).
