# Path-Following Platforms

## Context

The current `ENT_PLATFORM` only moves on the Y axis between `startY` and `endY` and doesn't carry the player (no kinematic body, no rider detection). This plan replaces it with a path-following platform that:

- moves between **multiple waypoints** in 3D (a polyline path),
- supports two motion modes — `once` (play to end and stop) or `ping_pong` (bounce forever),
- can be **enabled or disabled** at runtime (motion gate; collider stays put),
- **carries the player** standing on it (translation always, rotation around the leader's pivot),
- supports **multi-object platforms** (a leader entity drives motion, sibling entities in the same `group` ride along with rotated offsets),
- optionally **aligns its yaw to the path direction** (`face_path=1`) so a train car points down the track.

No existing levels use the old `start_y / end_y` platform — it's replaced wholesale, not kept as a legacy branch. Path nodes are a new entity type (`ENT_PATH_NODE`); they do not reuse `ENT_WAYPOINT` (nav-only).

## Rendering & physics envelope

Platforms get **kinematic box colliders** (same code path as `ENT_DOOR`). Motion runs **before** `physStep` so the character controller's gravity tick sees both the moved collider and the carried rider together — riders settle naturally with no popping. No new render features needed; this is all entity + physics work, well inside the GeForce 4 MX envelope.

---

## 1. Data model (`entity.h`)

**New enum + entity type:**

```c
enum PathMoveType { PATH_ONCE = 0, PATH_PING_PONG = 1 };

enum EntityType {
    ...
    ENT_PATH_NODE,     // position-only, no mesh/physics
};
```

**Replace the current `platform` struct in the union:**

```c
struct {
    char pathGroup[32];      // name of path_node group this platform follows
    PathMoveType moveType;
    float speed;             // m/s along path
    int enabled;
    int facePath;            // 1 = yaw aligns to segment heading, 0 = keep authored rotY
    float rotOffset;         // authored rotY at load time, added to path-derived yaw

    // Leader/sibling linkage (resolved at gameInit, see §3):
    int isLeader;
    int leaderIdx;           // entity index of leader (== self if leader)

    // Leader-only motion state:
    int segIdx;              // 0 = between node 0 and node 1
    float segT;              // 0..1 along current segment
    int dir;                 // +1 forward, -1 reverse
    int finished;            // PATH_ONCE reached the end

    // Sibling offset/yaw captured at gameInit, in leader-local space (unrotated):
    float offX, offY, offZ;
    float sibLocalAngle;     // sibling.rotY - leader.initialRotY
} platform;

struct { int order; } pathNode;
```

**Delete:** the old `startY / endY / state` fields, the `start_y= / end_y=` parser branches, and the `ENT_PLATFORM` cases in `entActivate` and `entUpdate`.

## 2. `.ent` authoring

```
path_node lift1_p0 lift1_path - 10 0 5 0 order=0
path_node lift1_p1 lift1_path - 10 4 5 0 order=1
path_node lift1_p2 lift1_path - 14 4 5 0 order=2

platform lift1_floor lift1 - 10 0 5 0 mesh=lift_floor.obj collide=box \
    path=lift1_path move=ping_pong speed=2 enabled=1
platform lift1_rail  lift1 - 10 0 5 0 mesh=lift_rail.obj  collide=box
```

New parser keys in `entLoadFile`:

| Key | Type | Notes |
|---|---|---|
| `order=` | int | for `path_node` only |
| `path=` | string | group name of `path_node` entities |
| `move=` | `once` \| `ping_pong` | leader only |
| `enabled=` | 0/1 | default 1 |
| `face_path=` | 0/1 | default 0 |
| `speed=` | float | already routed by type; extends platform branch |

Conventions:

- The **leader** is the first platform in a group that has `path=` set.
- Siblings carry only `mesh / collide`; their `path / move / speed` are ignored.
- The designer authors the leader at node 0's position (we snap on load for safety).
- Siblings' authored position is their resting pose relative to the leader at node 0.

## 3. Path table + leader resolution (new `path.h`)

Header-only, included after `entity.h`. Mirrors `nav.h`'s shape.

```c
struct PathGroup {
    char name[32];
    int  nodeEnts[32];      // entity indices, sorted by pathNode.order ascending
    int  nodeCount;
    float segLen[32];        // segLen[i] = |node[i+1] - node[i]|
};

struct PathTable { PathGroup groups[32]; int count; };

static void pathTableBuild(PathTable *pt, EntityList *el);
static PathGroup *pathTableFind(PathTable *pt, const char *name);
static Vec3 pathSample(PathGroup *pg, EntityList *el, int segIdx, float segT);
static void pathAdvance(PathGroup *pg, Entity *leader, float dt);
```

`pathTableBuild` algorithm:
1. Walk entities. For each `ENT_PATH_NODE`, push into the group keyed by `e->group`.
2. Sort each group's nodes by `pathNode.order`.
3. Precompute segment lengths.
4. Walk entities again. For each platform in a `group`, locate the leader (one with `pathGroup[0]!=0`).
   - **Leader:** `isLeader=1, leaderIdx=self, segIdx=0, segT=0, dir=+1`. Snap `pos` to `nodes[0]`. Record initial yaw for sibling-offset capture.
   - **Sibling:** `isLeader=0, leaderIdx=leader's idx`. Capture offset in leader-local (unrotated) space:

     ```c
     float r0 = leaderInitialYaw * M_PI / 180.0f;
     float c = cosf(r0), s = sinf(r0);
     float wx = sibling.posX - leader.posX;
     float wz = sibling.posZ - leader.posZ;
     sibling.platform.offX =  c * wx - s * wz;   // inverse rotation
     sibling.platform.offZ =  s * wx + c * wz;
     sibling.platform.offY = sibling.posY - leader.posY;
     sibling.platform.sibLocalAngle = sibling.rotY - leaderInitialYaw;
     ```

5. Log warnings: path group missing, group with no leader, leader referencing a non-existent path group, leader with `< 2` path nodes.

`Game` (in `game.h`) gains `PathTable paths;`. `gameInit` calls `pathTableBuild` after entity load, **before** the collider-build loop (so leader auto-snap happens before colliders are placed). `gameFree` doesn't need explicit teardown — plain POD.

## 4. Physics integration

Each `ENT_PLATFORM` (leader and siblings) gets a **kinematic box** collider, same as `ENT_DOOR`. In the collider-build loop in `game.h`, extend the door branch:

```c
if (e->type == ENT_DOOR || e->type == ENT_PLATFORM) {
    e->physBody = physAddKinematicBox(...);
}
```

`physRemoveStaticBox` in `gameFree` already handles kinematic bodies.

**New helper in `physics.h`** — avoids the convex sweep that doors use, since a rider standing on the platform would always block the sweep:

```c
static void physSetKinematicBoxTransform(PhysWorld *pw, void *bodyPtr,
                                         Vec3 center, float rotY);
// Same as physMoveKinematicBox minus the convexSweepTest — just sets the transform.
```

## 5. Motion step (`updatePlatforms` in `main.cpp`, mirrors `updateDoors`)

Called from the per-frame update sequence in `main()`, **before** `physStep`.

```c
static void updatePlatforms(EntityList *el, PathTable *pt, PhysWorld *pw, float dt)
{
    for each platform e where e->platform.isLeader && e->platform.enabled && !e->platform.finished:
        PathGroup *pg = pathTableFind(pt, e->platform.pathGroup);
        if (!pg || pg->nodeCount < 2) continue;

        // 1. Sample oldPos via lerp(nodes[segIdx], nodes[segIdx+1], segT).
        Vec3 oldPos = pathSample(pg, el, e->platform.segIdx, e->platform.segT);
        float oldYaw = e->rotY;

        // 2. Advance distance = speed * dt * dir along path, walking segments.
        //    Last node: PATH_ONCE -> finished=1, clamp;
        //               PATH_PING_PONG -> reverse dir, continue with remaining distance.
        //    Node 0 going backward: PING_PONG -> reverse again.
        pathAdvance(pg, e, dt);

        Vec3 newPos = pathSample(pg, el, e->platform.segIdx, e->platform.segT);

        // 3. Compute newYaw if face_path is on.
        float newYaw = oldYaw;
        if (e->platform.facePath) {
            Vec3 segDir = nodePos(pg, el, e->platform.segIdx + 1)
                        - nodePos(pg, el, e->platform.segIdx);
            // Pure vertical segment -> keep previous heading.
            if (segDir.x*segDir.x + segDir.z*segDir.z > 1e-6f) {
                float pathYaw = atan2f(segDir.x, segDir.z) * 180.0f / M_PI;
                newYaw = pathYaw + e->platform.rotOffset;
            }
        }
        float deltaYaw = newYaw - oldYaw;
        Vec3 delta = newPos - oldPos;

        // 4. Detect rider BEFORE applying transforms (raycast down from capsule).
        int riding = playerRidingGroup(pw, el, e);

        // 5. Apply to leader.
        e->posX = newPos.x; e->posY = newPos.y; e->posZ = newPos.z;
        e->rotY = newYaw;
        physSetKinematicBoxTransform(pw, e->physBody, colliderWorldCenter(e), newYaw);

        // 6. Apply to siblings — rotate stored offset by leader's current yaw, translate.
        float rad = newYaw * M_PI / 180.0f;
        float cs = cosf(rad), sn = sinf(rad);
        for each sibling m in same group:
            m->posX = newPos.x + (cs * m->platform.offX + sn * m->platform.offZ);
            m->posY = newPos.y +  m->platform.offY;
            m->posZ = newPos.z + (-sn * m->platform.offX + cs * m->platform.offZ);
            m->rotY = newYaw + m->platform.sibLocalAngle;
            physSetKinematicBoxTransform(pw, m->physBody, colliderWorldCenter(m), m->rotY);

        // 7. Carry the player: rotate rider's offset around leader by deltaYaw, then translate.
        if (riding) {
            btVector3 lead(oldPos.x, oldPos.y, oldPos.z);
            btVector3 playerOld = pw->ghostObject->getWorldTransform().getOrigin();
            btVector3 off = playerOld - lead;
            float r = deltaYaw * M_PI / 180.0f;
            float c = cosf(r), s = sinf(r);
            btVector3 offRot(c*off.x() + s*off.z(), off.y(), -s*off.x() + c*off.z());
            btVector3 leadNew(newPos.x, newPos.y, newPos.z);

            btTransform t = pw->ghostObject->getWorldTransform();
            t.setOrigin(leadNew + offRot);
            pw->ghostObject->setWorldTransform(t);
        }
}
```

Notes:

- **Yaw-only rotation** (no pitch/roll). Vertical-only path segments preserve the previous heading. Rationale: a tipping elevator feels wrong and breaks the world's up-axis assumption used by lighting and the character controller.
- **The player's view yaw is never modified.** A platform rotating under the player carries their position around the pivot but leaves mouse-look alone — standard Source-engine convention. If a level wants forced view rotation, Lua scripts can drive `g->yaw` themselves.

## 6. Carry-the-rider detection (`playerRidingGroup`)

Raycast straight down from the player capsule's center. Hit at distance ≤ `halfHeight + radius + 0.10m` against a body owned by any entity in the platform group ⇒ riding.

```c
static int playerRidingGroup(PhysWorld *pw, EntityList *el, Entity *leader)
{
    btVector3 from = pw->ghostObject->getWorldTransform().getOrigin();
    float reach = pw->capsuleShape->getHalfHeight()
                + pw->capsuleShape->getRadius() + 0.10f;
    btVector3 to = from + btVector3(0, -reach, 0);

    btCollisionWorld::ClosestRayResultCallback cb(from, to);
    cb.m_collisionFilterMask = btBroadphaseProxy::DefaultFilter;   // kinematic bodies
    pw->world->rayTest(from, to, cb);
    if (!cb.hasHit()) return 0;

    for each entity m where m->type == ENT_PLATFORM
                         && strcmp(m->group, leader->group) == 0:
        if ((btRigidBody *)m->physBody == cb.m_collisionObject) return 1;
    return 0;
}
```

Filter mask: kinematic boxes live on `DefaultFilter` (the default in `physAddKinematicBox`). Confirm during impl.

## 7. Activation semantics (`entActivate` in `entity.h`)

```c
case ENT_PLATFORM: {
    // Redirect to leader so activating a sibling-by-name still works.
    Entity *lead = (e->platform.isLeader) ? e : &el->entities[e->platform.leaderIdx];
    if (lead->platform.finished) {
        // PATH_ONCE replay: reset to start.
        lead->platform.segIdx = 0;
        lead->platform.segT = 0;
        lead->platform.dir = +1;
        lead->platform.finished = 0;
    }
    lead->platform.enabled = !lead->platform.enabled;
    break;
}
```

Switches and triggers toggle `enabled`. A finished `PATH_ONCE` replays when activated. `entActivate` cascades by group, so activating `lift1` calls into PLATFORM activation for every group member — the leader-redirect makes this idempotent (each call toggles the same leader flag once per group member; with N members it would toggle N times). Fix: in the PLATFORM case, only act if `e` is itself the leader, so siblings are no-ops within the same cascade. The redirect path is reserved for `entActivate(el, sibling_name)` direct calls.

```c
case ENT_PLATFORM:
    if (!e->platform.isLeader) break;   // sibling — skip; leader handles it
    if (e->platform.finished) { ...reset... }
    e->platform.enabled = !e->platform.enabled;
    break;
```

## 8. Debug visualisation

Piggyback the existing `B` (collider wireframes) toggle. Draw cyan line segments between consecutive path nodes per group, with small dots at each node. Add a short magenta forward-arrow at each leader's current position pointing along its current yaw — makes `face_path` failures obvious at a glance.

Cost is trivial (max 32 segments × 32 groups, plus one arrow per leader). No new key.

## 9. Test level

Add to `assets/levels/test_level.ent`:

- A 3-node path forming an L (ground → up → forward) and a 2-entity platform group (floor + railing) to prove sibling carrying and rotation.
- A switch wired to toggle `enabled`.

Place it near the spawn so manual testing is fast.

## 10. File touch list

| File | Change |
|---|---|
| `entity.h` | New enum members; new struct fields; parser branches for `path/move/enabled/face_path/order`; activation case |
| `path.h` | **New.** `PathTable`, `pathTableBuild`, `pathSample`, `pathAdvance`, `pathTableFind` |
| `physics.h` | New `physSetKinematicBoxTransform` helper |
| `game.h` | Include `path.h`; add `PathTable paths` to `Game`; call `pathTableBuild`; extend collider build to include `ENT_PLATFORM` as kinematic |
| `main.cpp` | Forward-include `path.h`; new `updatePlatforms` called before `physStep`; debug-draw path lines + heading arrows under `B` |
| `assets/levels/test_level.ent` | Sample platform group + path nodes + switch wiring |

## 11. Order of implementation

1. Add `ENT_PATH_NODE`, parser, `path.h` skeleton with table build + node sort.
2. Replace platform struct, parser keys, delete old code paths. Build still compiles, no motion yet.
3. Add `physSetKinematicBoxTransform`. Extend collider build for platforms (kinematic box).
4. Implement `pathSample`, `pathAdvance`, `updatePlatforms`. Skip carry-the-rider and `face_path` — verify visual motion only.
5. Add `face_path` heading computation and sibling-offset rotation.
6. Add `playerRidingGroup`; wire carry-the-rider (translation + rotation around pivot).
7. Activation → enabled toggle, with sibling-skip.
8. Debug-draw path lines + heading arrows under `B`.
9. Author the test_level lift.

Steps 1–3 are each a green compile; 4–8 add the moving parts incrementally so motion bugs can be `git bisect`ed.

## 12. Risks / known sharp edges

- **Instant yaw snap at node boundaries.** A 90° corner causes a 1-frame jump. The collider sweep is replaced by `physSetKinematicBoxTransform` (no sweep) so the corner is fast on the collider side, but visually it pops. Mitigation: designers add 2–3 extra nodes around corners to make a polyline arc. Smoothing in code (slerp over a fixed arc length) is a future enhancement.
- **Fast-descending platform vs. character controller.** If `speed` exceeds the character controller's per-frame fall delta, the player may briefly separate from the platform. Bullet's controller gravity is `-24 m/s²` here, so terminal velocity dwarfs any sane lift speed; cap initial designs at `speed ≤ 4 m/s`.
- **Rider sliding under sharp yaw.** With a fast `deltaYaw` and a rider near the edge, the carry-rotation may exceed what the character controller expects in one tick. If this becomes an issue, clamp the effective `deltaYaw` applied to riders to ~30°/frame.
- **`group` field overloaded.** `entActivate` already cascades by group. The leader-only branch in the PLATFORM case (§7) avoids double-toggles.
- **Rider yaw is intentionally not rotated.** A rotating platform carries the rider's position around the pivot, but their mouse-look yaw stays put. This is the standard FPS choice; rotating the view would fight mouse-look and feel awful.
- **Rotating platforms only support yaw.** Pitch/roll are out of scope. Roller-coaster-style cars would need a future `face_path=2` (full orientation) mode plus shape rebuilds for the collider; not v1.
