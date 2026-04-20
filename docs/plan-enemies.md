# Enemies + Navigation — Plan

## Goal

Add combat-capable AI enemies in the style of Half-Life 1. Five initial archetypes, all data-driven from the same underlying entity type:

| Enemy | Role | Speed | Attack | Range | Movement quirks |
|---|---|---|---|---|---|
| Zombie | melee brute | slow | swing (melee) | 1.5 m | shambles |
| Soldier | ranged | medium | rifle (hitscan) + melee up close | 20 m / 2 m | crouches/retreats optional later |
| Alien puppy (headcrab-ish) | fast pouncer | fast | leap (melee with jump) | 1.5 m | jumps toward player mid-chase |
| Alien dog (houndeye-ish) | fast meleeer | fastest | bite (melee) | 1.5 m | no special movement |
| Alien monster | ranged caster | medium | orb (projectile) + swipe up close | 15 m / 2 m | shoots arcing projectile |

## Scope choices

### Navigation: waypoint graph, not navmesh

Three realistic options:

1. **LOS-walk only.** Enemy walks straight toward player if line-of-sight is clear; otherwise idles. Fails badly in corridors / behind cover.
2. **Waypoint graph** (picked). Designer places `waypoint` entities in `.ent`, engine auto-connects any pair with mutual LOS. A* over the graph at runtime.
3. **Recast / Detour.** Modern industry standard. ~20k+ lines of C++, overkill for a Pentium-4 FPS, violates the "no heavy libraries" ethos.

**Waypoint graph** matches the HL1 era (their `info_node` system) and fits our Blender-authored workflow: drop empties in Blender, export as `waypoint` entities. Simple, debuggable, scales to rooms+corridors+vents.

### Enemy types: data-driven, one code type

Don't make `ENT_ZOMBIE` + `ENT_SOLDIER` + etc. — one `ENT_ENEMY` with fields that parameterize behavior. New types of enemy become new rows in `.ent` data, not new code.

### State machine: shared across all types

`IDLE → ALERT → CHASE → ATTACK → COOLDOWN → CHASE ...`, plus `HURT` and `DEAD` as transient overrides. Each enemy type just tunes timing, speeds, and attack selection.

### Projectiles: new entity type, minimal physics

`ENT_PROJECTILE` — spawned at runtime, given initial velocity, updates position + applies gravity optionally, raycast-sweeps per tick to check for hits against player and world. Despawns on hit or after max lifetime. Reuses existing entity array slots.

## Data model additions

### `.ent` syntax (proposed)

**Waypoint nodes** (for pathing):
```
waypoint room1_center  - 0   0 0 0
waypoint door_outer    - 4   0 0 0
waypoint corridor_mid  - 7.5 0 0 0
```
Edges auto-generated at load time: for each pair, `physRaycast` from A to B at eye height; if clear, add edge.

**Enemies**:
```
enemy zombie_01  - 5 0 -3 0 \
    iqm=models/zombie.iqm anim_idle=idle anim_walk=walk \
    anim_attack=bite anim_death=die \
    hp=50 speed=2.0 sight=20 \
    attack=melee attack_damage=15 attack_range=1.5 attack_cooldown=1.5

enemy soldier_01 - 10 0 5 0 \
    iqm=models/soldier.iqm anim_idle=stand anim_walk=run \
    anim_attack=fire anim_death=die \
    hp=75 speed=4 sight=30 \
    attack=hitscan attack_damage=12 attack_range=20 attack_cooldown=0.6

enemy alien_monster_01 - 3 0 8 90 \
    iqm=models/bigalien.iqm anim_walk=walk anim_attack=shoot anim_death=die \
    hp=120 speed=3 sight=25 \
    attack=projectile proj_mesh=models/orb.obj proj_speed=10 \
    proj_gravity=0 proj_damage=20 proj_radius=0.5 \
    attack_range=15 attack_cooldown=2.0
```

### Entity struct additions

`enemy` union member grows; fields used by attacks are guarded by `attack_type`:

```c
struct {
    int hp, maxHp;
    int state;                  /* IDLE/ALERT/CHASE/ATTACK/COOLDOWN/HURT/DEAD */
    float stateTimer;           /* time in current state */
    float speed;
    float sightRange;
    int attackType;             /* 0=melee, 1=hitscan, 2=projectile */
    float attackDamage;
    float attackRange;
    float attackCooldown;       /* seconds between attacks */
    float attackReadyAt;        /* timestamp */
    /* Projectile params (unused for melee/hitscan) */
    char projMesh[64];
    char projTex[64];
    float projSpeed;
    float projGravity;
    float projDamage;
    float projRadius;
    /* Anim index cache (resolved once at load time from anim_walk=, etc.) */
    int animIdle, animWalk, animAttack, animDeath, animHurt;
    /* Pathing */
    int pathNodeCount;
    int pathNodes[16];          /* indices into waypoint array */
    int pathStep;
    float repathAt;             /* only re-path every ~0.3s to save cost */
} enemy;
```

## New modules

### `nav.h`

Header-only. ~200 lines.

```c
struct NavGraph {
    Vec3 nodes[MAX_NODES];
    int numNodes;
    /* Adjacency bitset: 32 nodes per uint32, up to MAX_NODES/32 rows */
    unsigned int edges[MAX_NODES][MAX_NODES / 32 + 1];
};

static void navInit(NavGraph *g, EntityList *el, PhysWorld *pw);
    /* Gather all ENT_WAYPOINT entities → g->nodes.
       For each pair, physRaycast at y+0.5; add edge if no hit. */

static int navFindPath(NavGraph *g, Vec3 from, Vec3 to,
                       int *outNodes, int maxLen);
    /* Pick entry node (nearest visible from `from`), exit node (nearest
       visible from `to`), A* over graph, write node indices. */
```

A* implementation is classic — open list (priority queue), closed set, f = g + h with Euclidean heuristic. Static arrays sized for MAX_NODES, no heap allocation per search.

### `ai.h` or extend `entity.h`

Enemy update logic: per-tick state machine. Runs after `entUpdate()`, before `updateDoors()`:

```c
static void updateEnemies(EntityList *el, PhysWorld *pw, NavGraph *nav,
                          PlayerState *player, float dt);
```

Per enemy:

1. If DEAD — advance death anim, remove body from physics once it finishes.
2. Compute: distance to player, LOS to player (raycast), time since last state change.
3. State transitions:
   - **IDLE**: LOS to player within sightRange → ALERT
   - **ALERT**: brief pause (play alert anim / sound) → CHASE
   - **CHASE**: path to player via nav, walk toward next waypoint; if in attackRange and LOS → ATTACK
   - **ATTACK**: play attack anim, at "hit frame" apply damage, → COOLDOWN
   - **COOLDOWN**: wait attackCooldown seconds → CHASE (or idle if out of sight)
   - **HURT**: played briefly when taking damage from player; back to CHASE
   - **DEAD**: play death anim, remove collider

### `projectile.h`

Header-only. Each projectile is an entity of type `ENT_PROJECTILE` with:
- mesh (OBJ) + texture
- velocity (vec3)
- lifespan
- owner (who spawned it, to avoid self-damage)
- damage, damage radius

Per tick:
1. Apply gravity (if configured).
2. Raycast from current position along velocity×dt for this frame.
3. If hit static geometry → despawn, optional explosion particle later.
4. If hit is within player capsule radius → damage player, despawn.
5. Move.

## Player-side additions

### `PlayerState` struct (new, in main.cpp)

```c
struct PlayerState {
    int hp, maxHp;
    float damageFlashTimer;
};
```

HP starts at 100. Damage flash = 0.2s of red `uiQuad` overlay at 40% alpha. Negative HP → respawn (reset spawn position, refill HP) for now. Death screen / game over comes later.

### Player weapon damage

LMB click already plays gunshot. Add: raycast from camera forward, if hit is enemy, apply damage based on weapon (first weapon: 15 damage per shot, 1 shot per LMB). Enemy reacts with HURT state; if HP ≤ 0 → DEAD.

### HUD integration

The HUD bars we stubbed get real values:
```c
uiBar(uiRectMake(-halfW + pad, halfH - pad - barH, barW, barH),
      (float)player.hp / (float)player.maxHp, red);
snprintf(buf, sizeof(buf), "%d", player.hp);
uiText(..., buf, 3.5f);
```

## Phased implementation

### Phase 1 — Navigation foundation (2–3 days)
- `ENT_WAYPOINT` entity type + `.ent` parser
- `nav.h`: NavGraph, auto-edge generation on load, A* pathfinder
- Debug draw: press **N** to toggle nav graph rendering (nodes + edges as wireframe)
- No enemies yet — test by placing waypoints, visualizing graph, manually calling `navFindPath` from spawn to a known target and printing the path.

### Phase 2 — One enemy type: zombie (3–4 days)
- Extend `enemy` struct, parser, activation
- State machine in `updateEnemies`
- Zombie-specific behavior: slow walk, melee attack on contact
- Movement via kinematic body (like doors) — translate toward next path node, face player with rotY
- Animation hookup: play anim_walk while moving, anim_attack on melee
- Damage to player → player.hp -= damage, flash overlay
- Player shooting zombies: raycast on LMB, deal damage, zombie dies when hp ≤ 0

### Phase 3 — Soldier (hitscan) + projectile system (2–3 days)
- Hitscan attack: on attack frame, `physRaycast` from enemy eye to player; if hit is player, damage
- `ENT_PROJECTILE` entity + `projectile.h` module
- Spawn helper: `projSpawn(el, mesh, pos, vel, owner, damage)`
- Projectile update runs each frame before enemy update

### Phase 4 — Remaining enemies (2 days)
- Alien puppy: jump attack (give upward velocity + horizontal lunge when close)
- Alien dog: same structure as zombie, faster, different anim/model
- Alien monster: projectile attack reusing phase-3 system

### Phase 5 — Polish (1–2 days)
- Muzzle flash for soldier (particle, separate from phase 3)
- Death animations play out, corpses stay for a few seconds then fade
- Sound hookups: footsteps per enemy, attack sounds, hurt/death grunts
- Damage number popups (optional, via `uiText` at enemy position projected to screen)

## Risks / open questions

- **Movement**: will enemies use full `btKinematicCharacterController` like the player, or simpler kinematic bodies? The controller gives us free stair/slope handling but is heavy per enemy (one ghost object each, character action in world). Simpler approach: kinematic body + manual raycast for stair detection. Start simple, upgrade if AI walks into walls badly.
- **LOS checks** cost one raycast per enemy per frame. For 10+ enemies visible at once, still cheap. Cache the result across frames if it becomes a hotspot.
- **Jump attack** (alien puppy) requires dynamic body or kinematic body with manual ballistics — need to pick one. Probably manual ballistics per projectile-path is simpler.
- **Animation "hit frames"** — how does the engine know when in the attack anim to deal damage? Options: hardcoded 50% through the anim; or an anim event system (named keyframes) later.
- **Corpse management**: enemies never "leave" the entity array; we mark dead and skip updates. After a timer, remove collider but keep mesh for a few seconds, then set `active=0`.
- **Save/load**: not in scope. Game state is per-session.

## Non-goals

- Faction AI (allies vs enemies). All non-player is hostile.
- Enemy vs enemy combat.
- Group behavior (flanking, covering fire). Each enemy independent.
- Advanced pathfinding (dynamic obstacle avoidance beyond re-path). Designer places waypoints well enough.
- Enemy climbing ladders / using doors. Doors block AI like they block the player; designer places waypoints around them.
