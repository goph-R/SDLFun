#ifndef GAME_H
#define GAME_H

/*
 * One active gameplay session. Holds the level OBJ, entities, physics world,
 * nav graph, dynamic lightmap, per-game texture cache, and the player's
 * transient state (yaw/pitch/velocity, flashlight on/off, FPS counter, etc.).
 *
 * Long-lived app systems (UI, audio, script runtime, asset registry) are
 * passed in by pointer — they persist across game restarts. gameInit and
 * gameFree cleanly set up and tear down the per-game state so "start new
 * game" can run twice in a row without leaks.
 *
 * Must be included after the engine subsystem headers (obj_loader.h,
 * texture.h, iqm.h, asset_registry.h, entity.h, flashlight.h, physics.h,
 * nav.h, ui.h, script.h) — it doesn't re-include them, matching the
 * header-only module convention used elsewhere in the engine.
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

static int gameInit(Game *g,
                    const char *levelObj, const char *levelEnt,
                    ScriptSystem *script, AssetRegistry *assetReg)
{
    memset(g, 0, sizeof(*g));
    texCacheInit(&g->texCache);

    objInit(&g->level);
    if (!objLoad(&g->level, levelObj)) {
        conLogf("game: failed to load %s\n", levelObj);
        return 0;
    }
    conLogf("Level loaded: %d verts, %d texcoords, %d tris, %d materials\n",
           g->level.numVerts, g->level.numTexcoords,
           g->level.numTris, g->level.numMaterials);

    g->entities = (EntityList *)malloc(sizeof(EntityList));
    entListInit(g->entities);
    script->entities = g->entities;  /* Rewire script to this game's list. */

    entLoadFile(g->entities, levelEnt, &g->texCache, assetReg);

    float spawnX = 0.0f, spawnY = 2.0f, spawnZ = 0.0f;
    if (g->entities->playerIndex >= 0) {
        Entity *pe = &g->entities->entities[g->entities->playerIndex];
        spawnX = pe->posX;
        spawnY = pe->posY;
        spawnZ = pe->posZ;
    }

    physInit(&g->phys);
    physLoadLevel(&g->phys, &g->level);
    physCreatePlayer(&g->phys, spawnX, spawnY, spawnZ);

    /* Sector-sorting reorders mesh->tris in place — must happen AFTER
       physLoadLevel has captured the triangles, else the physics trimesh
       would see a shuffled indexing. */
    objBuildSectors(&g->level);

    /* Decoration colliders — box (AABB from mesh verts) or trimesh
       (exact btBvhTriangleMeshShape). AABB fills hollow space in concave
       props; use trimesh for desks / chairs with kneeholes. */
    for (int i = 0; i < g->entities->count; i++) {
        Entity *e = &g->entities->entities[i];
        if (!e->collide || !e->hasMesh || e->mesh.numVerts == 0) continue;

        float s = (e->scale > 0.0f) ? e->scale : 1.0f;

        if (e->collide == 2) {
            e->physBody = physAddStaticTrimesh(&g->phys, &e->mesh,
                                               e->posX, e->posY, e->posZ,
                                               e->rotY, s);
            conLogf("entity: %s collider (trimesh %d tris at %.2f,%.2f,%.2f)\n",
                   e->name, e->mesh.numTris, e->posX, e->posY, e->posZ);
            continue;
        }

        Vec3 *v0 = &e->mesh.verts[0];
        float minX = v0->x, maxX = v0->x;
        float minY = v0->y, maxY = v0->y;
        float minZ = v0->z, maxZ = v0->z;
        for (int k = 1; k < e->mesh.numVerts; k++) {
            Vec3 *v = &e->mesh.verts[k];
            if (v->x < minX) minX = v->x; else if (v->x > maxX) maxX = v->x;
            if (v->y < minY) minY = v->y; else if (v->y > maxY) maxY = v->y;
            if (v->z < minZ) minZ = v->z; else if (v->z > maxZ) maxZ = v->z;
        }

        float hx = (maxX - minX) * 0.5f * s;
        float hy = (maxY - minY) * 0.5f * s;
        float hz = (maxZ - minZ) * 0.5f * s;
        float lcx = (minX + maxX) * 0.5f * s;
        float lcy = (minY + maxY) * 0.5f * s;
        float lcz = (minZ + maxZ) * 0.5f * s;

        float rad = e->rotY * (float)M_PI / 180.0f;
        float cs = cosf(rad), sn = sinf(rad);
        float wx = e->posX + (cs * lcx + sn * lcz);
        float wy = e->posY + lcy;
        float wz = e->posZ + (-sn * lcx + cs * lcz);

        if (e->type == ENT_DOOR) {
            e->door.lcx = lcx;
            e->door.lcy = lcy;
            e->door.lcz = lcz;
            e->physBody = physAddKinematicBox(&g->phys, wx, wy, wz,
                                              hx, hy, hz, e->rotY);
            conLogf("entity: %s door (kinematic box %.2fx%.2fx%.2f)\n",
                   e->name, hx*2, hy*2, hz*2);
        } else {
            e->physBody = physAddStaticBox(&g->phys, wx, wy, wz,
                                           hx, hy, hz, e->rotY);
            conLogf("entity: %s collider (box %.2fx%.2fx%.2f at %.2f,%.2f,%.2f)\n",
                   e->name, hx*2, hy*2, hz*2, wx, wy, wz);
        }
    }

    /* Dynamic flashlight lightmap (HL1-style: CPU-modified texels re-uploaded
       each frame). Only engages if a level lightmap texture is available. */
    g->hasDynLm = 0;
    {
        GLuint lmTex = texCacheGet(&g->texCache,
                                   "assets/levels/lightmap.png", GL_CLAMP_TO_EDGE);
        if (lmTex) {
            g->hasDynLm = dynLmInit(&g->dynLm, "assets/levels/lightmap.png",
                                    &g->level, lmTex);
        }
    }

    /* Nav graph — pairs up waypoint entities with pairwise LOS. Empty
       graph if no waypoints placed. */
    navInit(&g->nav, g->entities, &g->phys);

    /* Game scripts run once everything else is wired up. main.lua defines
       hooks; on_start fires once so any HUD messages or initial switch
       activations land on frame 0. */
    scriptRunFile(script, "scripts/main.lua");
    scriptCall(script, "on_start");

    return 1;
}

static void gameFree(Game *g, ScriptSystem *script)
{
    /* Tear down entity colliders before physics cleanup. */
    if (g->entities) {
        for (int i = 0; i < g->entities->count; i++) {
            Entity *e = &g->entities->entities[i];
            if (!e->physBody) continue;
            if (e->collide == 2) physRemoveStaticTrimesh(&g->phys, e->physBody);
            else                 physRemoveStaticBox(&g->phys, e->physBody);
            e->physBody = NULL;
        }
        entListFree(g->entities);
        free(g->entities);
        g->entities = NULL;
    }
    if (script) script->entities = NULL;

    if (g->hasDynLm) { dynLmFree(&g->dynLm); g->hasDynLm = 0; }
    texCacheFree(&g->texCache);
    physCleanup(&g->phys);
    objFree(&g->level);
}

#endif /* GAME_H */
