#ifndef GAME_SESSION_H
#define GAME_SESSION_H

/*
 * Full gameplay-session lifecycle: gameInit / gameFree. Split out of game.h
 * so that the struct definition (game.h) can be included by tools that DON'T
 * want the script runtime — notably the SOOB level editor, which builds a
 * Game via editLoadLevel (edit_load.h) and never touches Lua, physics setup,
 * nav, or the flashlight lightmap.
 *
 * gameInit/gameFree pull in the script runtime (scriptCall / scriptExt*), so
 * this header must be included AFTER script.h and script_ext.h — as main.cpp
 * does. game.h (the struct) plus the subsystem headers it references
 * (obj_loader.h, texture.h, entity.h, physics.h, nav.h, path.h, flashlight.h)
 * must already be included too.
 */

#include "game.h"

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
    scriptExtSetEntities(g->entities);  /* Wire ent_activate to this list. */

    entLoadFile(g->entities, levelEnt, &g->texCache, assetReg);

    /* Build the platform path table BEFORE the collider-build loop so leader
       auto-snap (move leader to its path's node 0) has happened by the time
       we construct kinematic bodies. */
    pathTableBuild(&g->paths, g->entities);

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
            Vec3 ec = { e->posX, e->posY, e->posZ };
            e->physBody = physAddStaticTrimesh(&g->phys, &e->mesh, ec, e->rotY, s);
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

        Vec3 wc = { wx, wy, wz };
        Vec3 he = { hx, hy, hz };
        if (e->type == ENT_DOOR) {
            e->door.lcx = lcx;
            e->door.lcy = lcy;
            e->door.lcz = lcz;
            e->physBody = physAddKinematicBox(&g->phys, wc, he, e->rotY);
            conLogf("entity: %s door (kinematic box %.2fx%.2fx%.2f)\n",
                   e->name, hx*2, hy*2, hz*2);
        } else if (e->type == ENT_PLATFORM) {
            /* Cache local-space AABB center so updatePlatforms() can
               recompute the world collider center each frame after the
               platform's rotation/translation changes. */
            e->platform.lcx = lcx;
            e->platform.lcy = lcy;
            e->platform.lcz = lcz;
            e->physBody = physAddKinematicBox(&g->phys, wc, he, e->rotY);
            conLogf("entity: %s platform (kinematic box %.2fx%.2fx%.2f)\n",
                   e->name, hx*2, hy*2, hz*2);
        } else {
            e->physBody = physAddStaticBox(&g->phys, wc, he, e->rotY);
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

    /* main.lua is now loaded once at app boot (see main.cpp), not per
       session — the Lua menu state outlives gameInit/gameFree cycles.
       onStart still fires here so per-session setup (welcome message,
       initial switch activations) lands on frame 0. */
    scriptCall(script, "onStart");

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
    scriptExtSetEntities(NULL);

    if (g->hasDynLm) { dynLmFree(&g->dynLm); g->hasDynLm = 0; }
    texCacheFree(&g->texCache);
    physCleanup(&g->phys);
    objFree(&g->level);
}

#endif /* GAME_SESSION_H */
