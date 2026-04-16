# Code Restructuring + Entity Component System — Draft

## Big Picture

The engine is currently a single `main.cpp` with header-only modules (physics.h, texture.h,
obj_loader.h). Everything is hardcoded: one player, one level, no dynamic objects. To support
entities (enemies, items, decorations, platforms, etc.), we need:

1. **Code separation** — split the monolith into manageable modules
2. **Entity system** — a simple component-based architecture for game objects
3. **Blender integration** — entities placed in Blender, exported with the level

## Current Code Structure

```
main.cpp        — 612 lines: init, input, game loop, rendering, sound, cleanup
physics.h       — 146 lines: Bullet Physics world, character controller, ceiling clamp
obj_loader.h    — 285 lines: OBJ/MTL parsing, materials, sectors
texture.h       — 220 lines: BMP/TGA loading, texture cache
```

Everything lives in one flat structure. The game loop mixes input, physics, rendering, and
sound in a single while loop.

## Proposed Code Structure

```
main.cpp            — entry point, SDL init, game loop (thin orchestrator)
engine/
  renderer.h        — OpenGL setup, renderLevel, renderGun, renderCrosshair, renderEntity
  input.h           — keyboard/mouse state, event processing
  audio.h           — FMOD init, sound creation, playback helpers
  camera.h          — yaw, pitch, FPS camera logic
physics.h           — unchanged (Bullet Physics world + character)
obj_loader.h        — unchanged (OBJ/MTL parsing)
texture.h           — unchanged (BMP/TGA + cache)
iqm.h              — IQM model loader + skeletal animation (CPU skinning)
entity.h            — entity system: Entity, Component types, entity list
entity_types.h      — concrete component behaviors (enemy AI, platform movement, etc.)
level.h             — level loading: geometry + entities from file
```

The header-only style is kept (no .cpp files besides main.cpp) for Win98 build simplicity.

## Entity Component System

### Design Principles

- **Simple**: no templates, no virtual dispatch, no RTTI — C-style structs with type tags
- **C++98 compatible**: fixed arrays, no STL, no new C++ features
- **Data-driven**: entities defined in level files, not hardcoded
- **Flat**: components are inline in the entity struct, not heap-allocated

### Entity Structure

```c
#define MAX_ENTITIES 256

enum EntityType {
    ENT_NONE = 0,
    ENT_PLAYER,
    ENT_DECORATION,     /* static prop (baked into lightmap) or dynamic prop */
    ENT_ITEM,           /* pickup: health, ammo, key, etc. */
    ENT_ENEMY,          /* AI-controlled hostile */
    ENT_PLATFORM,       /* moving platform (elevator, lift) */
    ENT_SWITCH,         /* activatable: button, lever */
    ENT_TRIGGER         /* invisible volume: door trigger, zone, hurt, etc. */
};

struct Entity {
    int active;
    EntityType type;
    char name[32];          /* optional, for scripting/targeting */
    char group[32];         /* optional, for batch operations */

    /* Transform */
    float posX, posY, posZ;
    float rotY;             /* yaw rotation in degrees */
    float scaleX, scaleY, scaleZ;

    /* Visual (optional — not all entities are visible) */
    int hasMesh;
    ObjMesh mesh;           /* loaded from .obj file (static models) */
    GLuint diffuseTex;      /* texture for this entity's mesh */
    int isStatic;           /* 1 = baked into lightmap, immovable */

    /* Animated model (optional — overrides mesh if present) */
    int hasAnim;
    IqmModel *model;        /* IQM skeletal model (bones + weighted verts) */
    int currentAnim;         /* index of playing animation sequence */
    float animTime;          /* current time within the animation */
    float animSpeed;         /* playback speed multiplier, default 1.0 */

    /* Physics (optional) */
    int hasCollision;
    btRigidBody *body;          /* for dynamic/kinematic objects */
    btCollisionShape *shape;    /* collision shape */

    /* Type-specific data (union to save memory) */
    union {
        struct {            /* ENT_ITEM */
            int itemType;   /* health=0, ammo=1, key=2, etc. */
            int respawn;    /* seconds until respawn, 0=no respawn */
        } item;

        struct {            /* ENT_ENEMY */
            int health;
            int state;      /* 0=idle, 1=patrol, 2=chase, 3=attack, 4=dead */
            float speed;
            float sightRange;
            int targetEntity; /* index of current target (usually player) */
        } enemy;

        struct {            /* ENT_PLATFORM */
            float startY, endY;   /* movement range */
            float speed;
            int state;            /* 0=at start, 1=moving to end, 2=at end, 3=moving to start */
            char targetGroup[32]; /* optional: activate when triggered */
        } platform;

        struct {            /* ENT_SWITCH */
            int state;            /* 0=off, 1=on */
            char targetName[32];  /* entity name or group to activate */
        } sw;

        struct {            /* ENT_TRIGGER */
            float sizeX, sizeY, sizeZ;  /* trigger volume half-extents */
            char targetName[32];         /* what to activate on enter */
            int triggerOnce;             /* 1=disable after first trigger */
            int triggered;
        } trigger;
    };
};
```

### Entity List

```c
struct EntityList {
    Entity entities[MAX_ENTITIES];
    int count;
    int playerIndex;    /* shortcut to the player entity */
};
```

### Core Operations

```c
int    entityCreate(EntityList *list, EntityType type);  /* returns index */
void   entityDestroy(EntityList *list, int index);
Entity *entityFindByName(EntityList *list, const char *name);
void   entityActivateGroup(EntityList *list, const char *group);

void   entitiesUpdate(EntityList *list, PhysWorld *pw, float dt);  /* per-frame logic */
void   entitiesRender(EntityList *list, TexCache *cache);          /* draw all visible */
```

### Per-Type Update Logic

Each frame, `entitiesUpdate` loops through active entities and dispatches by type:

- **ENT_PLAYER**: handled by existing character controller (physics.h), entity just tracks position
- **ENT_DECORATION**: no update if static. Dynamic decorations get physics simulation.
- **ENT_ITEM**: check distance to player, pick up if close enough, play sound
- **ENT_ENEMY**: simple state machine (idle -> patrol -> chase -> attack), raycasts for sight
- **ENT_PLATFORM**: lerp between startY/endY based on state, move physics body
- **ENT_SWITCH**: check if player activates (use key), toggle state, send activation to target
- **ENT_TRIGGER**: test player overlap with AABB, fire activation once or repeatedly

### Activation / Messaging

Entities communicate through name/group targeting:
- A switch with `targetName = "door_01"` activates the entity named `door_01`
- A trigger with `targetName = "elevator_group"` activates all entities in group `elevator_group`
- Activation means different things per type: platform starts moving, switch toggles, etc.

## Blender Integration for Entities

### How Entities Are Placed

In Blender, entities are **Empty objects** or **mesh objects** placed in the scene:

- The object name becomes the entity name (e.g., `enemy_guard_01`)
- A custom property `entity_type` defines the type (e.g., `enemy`, `item`, `platform`)
- A custom property `group` defines the group (optional)
- Position/rotation/scale come from the object's transform
- If it has mesh data, it becomes the entity's visual model

### Static Decorations and Lightmaps

Static decorations (`isStatic = 1`) should be **included in the lightmap bake** because they
affect lighting (cast shadows, receive light). In Blender:

- Static decorations are regular mesh objects included in the scene during baking
- They are joined with (or placed near) the level geometry so Cycles casts their shadows
- At export time, they are separated back out as entities with `isStatic = 1`
- The engine renders them but doesn't update their physics

Dynamic decorations (pushable crates, etc.) are NOT baked — they use the level's ambient
lighting or a simplified light probe.

### Entity File Format

Entities are stored in a companion file alongside the OBJ, e.g., `test_level.ent`:

```
# Entity definitions for test_level
# Format: type name group posX posY posZ rotY [type-specific key=value pairs]

player spawn_point - 0 2 0 0
decoration barrel_01 props 5 0.5 3 0 mesh=barrel.obj tex=barrel.bmp static=1
decoration barrel_02 props 5 0.5 -3 45 mesh=barrel.obj tex=barrel.bmp static=1
item health_pack_01 - 10 2.5 0 0 item_type=0
enemy guard_01 enemies 20 2.5 -4 180 health=100 speed=3 sight=15
platform lift_01 elevator_group 20 2 4 0 start_y=2 end_y=6 speed=2
switch button_01 - 18 3 5 0 target=elevator_group
trigger zone_01 - 15 3 0 0 size=3,4,3 target=guard_01 once=1
```

### Export Workflow

The Blender export plugin (future Phase 2) will:
1. Export level geometry as OBJ+MTL (existing)
2. Scan scene for entity objects (by custom property)
3. Write `.ent` file with entity definitions
4. Copy entity mesh files (barrel.obj, etc.) to export directory

## Rendering Entities

Entity meshes are rendered after the level, using the same OpenGL pipeline:

```c
void entitiesRender(EntityList *list, TexCache *cache)
{
    for (int i = 0; i < list->count; i++) {
        Entity *e = &list->entities[i];
        if (!e->active || !e->hasMesh) continue;

        glPushMatrix();
        glTranslatef(e->posX, e->posY, e->posZ);
        glRotatef(e->rotY, 0, 1, 0);
        glScalef(e->scaleX, e->scaleY, e->scaleZ);

        if (e->diffuseTex) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, e->diffuseTex);
        }

        /* Render entity mesh (simple single-material path) */
        glBegin(GL_TRIANGLES);
        for (int t = 0; t < e->mesh.numTris; t++) {
            /* ... vertex/normal/texcoord submission ... */
        }
        glEnd();

        if (e->diffuseTex) glDisable(GL_TEXTURE_2D);
        glPopMatrix();
    }
}
```

## Skeletal Animation (IQM Format)

### Why IQM

IQM (Inter-Quake Model) is a compact binary format designed for indie/retro game engines.
It supports skeletal animation with bones, vertex weights, and multiple named animation
sequences. The reference C loader is ~300 lines with no dependencies. A well-maintained
Blender exporter exists at https://github.com/lsalzman/iqm.

### IQM Data Model

```c
struct IqmBone {
    char name[32];
    int parent;         /* -1 for root */
    float basePos[3];   /* bind pose position */
    float baseRot[4];   /* bind pose rotation (quaternion) */
    float baseScale[3]; /* bind pose scale */
};

struct IqmVertex {
    float pos[3];
    float normal[3];
    float texcoord[2];
    unsigned char boneIndex[4];   /* up to 4 bone influences */
    unsigned char boneWeight[4];  /* weights (0-255, sum to 255) */
};

struct IqmAnim {
    char name[32];      /* e.g., "idle", "walk", "attack", "death" */
    int firstFrame;
    int numFrames;
    float framerate;
    int loop;           /* 1 = loops, 0 = plays once */
};

struct IqmModel {
    IqmVertex *verts;
    int numVerts;
    int *tris;          /* triangle indices (numTris * 3) */
    int numTris;

    IqmBone *bones;
    int numBones;

    IqmAnim *anims;
    int numAnims;

    /* Per-frame bone poses: numBones * totalFrames transforms */
    float *framePoses;  /* array of 4x3 matrices (or pos+quat+scale) */
    int totalFrames;

    /* Scratch buffer for CPU skinning (rewritten every frame) */
    float *skinnedVerts;  /* numVerts * 3 floats (transformed positions) */
    float *skinnedNorms;  /* numVerts * 3 floats (transformed normals) */

    GLuint diffuseTex;
};
```

### CPU Skinning Pipeline (per frame)

This runs on the CPU — no shaders needed, works on OpenGL 1.x:

```
1. Evaluate animation:
   - Find current frame from animTime + framerate
   - Interpolate between frame N and N+1 (lerp pos/scale, slerp rotation)
   - Compute per-bone world matrices (walk bone hierarchy, parent * local)

2. Skin vertices:
   - For each vertex, blend up to 4 bone matrices weighted by boneWeight[]
   - Transform position and normal by the blended matrix
   - Write to skinnedVerts[] / skinnedNorms[] scratch buffers

3. Render:
   - glBegin(GL_TRIANGLES)
   - Submit skinnedVerts/skinnedNorms/texcoords per triangle
   - glEnd()
```

On a Pentium 4, skinning a 300-vertex character at 30fps is trivial (~0.1ms).

### Blender Workflow

1. Model character in Blender (low-poly, 200-500 triangles for retro look)
2. Create armature (skeleton), parent mesh to armature with automatic weights
3. Create animation actions: idle, walk, run, attack, pain, death
4. Install the IQM exporter addon (from https://github.com/lsalzman/iqm)
5. Export: `File > Export > Inter-Quake Model (.iqm)`
   - Select mesh + armature
   - Check "Animations" and pick which actions to include
6. Place the `.iqm` file in the game directory

### Animation Playback in Entity System

```c
/* Set animation by name */
void entityPlayAnim(Entity *e, const char *animName, int loop);

/* Update animation (called from entitiesUpdate) */
void entityUpdateAnim(Entity *e, float dt)
{
    if (!e->hasAnim || !e->model) return;
    IqmAnim *anim = &e->model->anims[e->currentAnim];

    e->animTime += dt * e->animSpeed * anim->framerate;

    if (anim->loop) {
        /* Wrap around */
        while (e->animTime >= anim->numFrames)
            e->animTime -= anim->numFrames;
    } else {
        /* Clamp to last frame */
        if (e->animTime >= anim->numFrames - 1)
            e->animTime = (float)(anim->numFrames - 1);
    }

    iqmSkin(e->model, e->currentAnim, e->animTime);
}
```

### Animation Blending (Future)

For smooth transitions (e.g., idle -> walk), blend between two animations:
- Store `prevAnim`, `blendTime`, `blendDuration`
- During blend: skin both poses, lerp the final vertex positions
- Simple linear blend is enough for retro aesthetics

### .ent File Integration

Animated entities reference `.iqm` files instead of `.obj`:

```
enemy guard_01 enemies 20 2.5 -4 180 iqm=guard.iqm tex=guard.bmp health=100 speed=3
decoration torch_01 props 5 3 0 0 iqm=torch.iqm tex=torch.bmp static=1 anim=flicker
```

The `iqm=` key tells the loader to use IQM instead of OBJ. The `anim=` key sets the
initial animation sequence by name.

### File Structure

```
models/
  guard.iqm           — enemy model with walk/attack/death animations
  guard.bmp           — enemy texture (24-bit, small: 128x128 or 256x256)
  torch.iqm           — animated torch prop (flicker animation)
  torch.bmp           — torch texture
  barrel.obj          — static prop (no animation, use OBJ)
  barrel.bmp          — barrel texture
```

## Implementation Phases

### Phase A: Code Separation
Split main.cpp into header modules (renderer.h, input.h, audio.h, camera.h).
No new features — just reorganization. Must still compile and work identically.

### Phase B: IQM Loader + Skeletal Animation
Add iqm.h with IQM binary loader, bone hierarchy evaluation, CPU skinning,
and OpenGL 1.x rendering. Test with a simple animated model from Blender.

### Phase C: Entity Core
Add entity.h with Entity struct, EntityList, create/destroy/find operations.
Add .ent file parser. Load and render decoration entities (static OBJ meshes
and animated IQM models). Player becomes entity index 0.

### Phase D: Interactive Entities
Add item pickup, switch activation, trigger volumes, platform movement.
Entity-to-entity messaging via name/group targeting.

### Phase E: Enemy AI
Basic state machine: idle, patrol, chase, attack.
Line-of-sight raycasts via Bullet Physics.
Simple pathfinding (waypoint-based or direct chase).
Animation driven by AI state (idle->walk->attack->death).

### Phase F: Blender Export Plugin
Extend sdlfun_export.py to scan for entities, write .ent files,
export entity meshes (OBJ for static, IQM for animated),
handle static decoration lightmap workflow.

## Open Questions

1. **Entity mesh format**: OBJ for static props, IQM for animated models.
   Both loaders already planned. Entity decides at load time based on
   `mesh=` (OBJ) vs `iqm=` (IQM) key in .ent file.

2. **Physics for entities**: Full Bullet rigid bodies or simplified AABB checks?
   Recommendation: AABB for triggers/items (cheap), Bullet rigid bodies for
   pushable decorations and platforms (need proper collision response).

3. **Enemy pathfinding**: Waypoints or navmesh?
   Recommendation: waypoints for now — navmesh is overkill for this engine.
   Waypoints are entities too (type=waypoint, invisible).

4. **Entity limits**: MAX_ENTITIES=256 enough?
   For a retro engine with <5000 tri levels, 256 is generous.

5. **Sound per entity**: Should entities have sound components?
   Could add a simple `soundId` field for ambient/loop sounds (torch crackle, etc.).

6. **IQM vertex budget**: For Win98/P4 target, keep characters at 200-500 triangles.
   CPU skinning cost is ~0.1ms per 300-vert model on P4. With 10 visible
   animated entities, that's ~1ms total — well within budget.

7. **Animation blending**: Simple linear blend between two poses is enough
   for retro aesthetics. Implement after basic playback works.
