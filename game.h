#ifndef GAME_H
#define GAME_H

/*
 * One active gameplay session. Holds the level OBJ, entities, physics world,
 * nav graph, dynamic lightmap, per-game texture cache, and the player's
 * transient state (yaw/pitch/velocity, flashlight on/off, FPS counter, etc.).
 *
 * This header is JUST the struct definition. The gameplay-session lifecycle
 * (gameInit / gameFree) lives in game_session.h, which pulls in the script
 * runtime — split out so tools that only need the struct (the SOOB level
 * editor, via edit_load.h) can include this without dragging in Lua / UI /
 * audio. The game (main.cpp) includes game_session.h after script.h.
 *
 * Must be included after the engine subsystem headers that define the field
 * types (obj_loader.h, texture.h, iqm.h, entity.h, flashlight.h, physics.h,
 * nav.h, path.h) — it doesn't re-include them, matching the header-only
 * module convention used elsewhere in the engine.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Game {
    ObjMesh      level;
    EntityList  *entities;    /* heap-allocated (~4MB); freed by gameFree */
    PhysWorld    phys;
    NavGraph     nav;
    PathTable    paths;       /* platform motion paths (see path.h) */
    DynLightmap  dynLm;
    int          hasDynLm;
    TexCache     texCache;

    /* Camera / player state. */
    float yaw, pitch;
    float velX, velZ;         /* horizontal velocity (m/s) */

    /* Per-game timers and toggles. */
    int   gunFlashTimer;
    int   footstepTimer;
    int   flashlightOn;
    int   debugColliders;
    int   debugNav;

    /* FPS counter (reset each game). */
    float fpsAccum;
    int   fpsFrames;
    int   fpsDisplay;
};

#endif /* GAME_H */
