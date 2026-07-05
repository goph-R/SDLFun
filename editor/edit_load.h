#ifndef EDIT_LOAD_H
#define EDIT_LOAD_H

/* ---- Editor level load (header-only, static) ----
 *
 * Editor-only counterpart to gameInit/gameFree (game.h). Populates a Game
 * with JUST the renderable state the SOOB level editor needs —
 *
 *     OBJ mesh  +  material sectors  +  entities  +  texture cache
 *
 * — and deliberately skips everything gameInit does that only matters to a
 * running simulation:
 *
 *     - physics (physInit / physLoadLevel / physCreatePlayer, and the whole
 *       per-entity collider-build loop),
 *     - the nav graph (navInit),
 *     - the flashlight dynamic lightmap (dynLmInit) — a runtime-only effect;
 *       the STATIC baked lightmaps still draw, since renderLevelSectored
 *       loads them lazily from each material's lightmapPath,
 *     - Lua (scriptExtSetEntities / onStart).
 *
 * The result is a Game you can pass straight to renderWorld() (render_world.h)
 * with an editor free-fly Camera. The physics/nav fields stay zeroed and
 * untouched — editFreeLevel does NOT call physCleanup, so it's safe to tear
 * down a Game that was never physInit'd.
 *
 * NOTE: because Game embeds PhysWorld / NavGraph by value, the editor TU still
 * compiles physics.h / nav.h (for the struct layout) and links Bullet, even
 * though it never steps a simulation. Making those pointers is a later,
 * larger refactor; not needed to get the editor rendering.
 *
 * Entities load in their authored pose. entUpdate() is simulation and is NOT
 * called here — if the editor wants animated IQM previews it can call
 * entUpdate(g->entities, camX, camY, camZ, dt) itself each frame (it must set
 * scriptExtSetEntities first if any entity triggers touch Lua).
 *
 * Include order (single-TU header convention): after game.h (which itself
 * needs obj_loader.h, texture.h, entity.h, asset_registry.h, etc.).
 */

static int editLoadLevel(Game *g,
                         const char *levelObj, const char *levelEnt,
                         AssetRegistry *assetReg)
{
    memset(g, 0, sizeof(*g));
    texCacheInit(&g->texCache);

    objInit(&g->level);
    if (!objLoad(&g->level, levelObj)) {
        conLogf("edit: failed to load %s\n", levelObj);
        return 0;
    }
    conLogf("Edit level loaded: %d verts, %d texcoords, %d tris, %d materials\n",
           g->level.numVerts, g->level.numTexcoords,
           g->level.numTris, g->level.numMaterials);

    /* Sector-sort right after load. In gameInit this must wait until AFTER
       physLoadLevel (sorting reorders mesh->tris, which would desync the
       physics trimesh) — but with no physics here there's no such ordering
       constraint, so renderLevelSectored gets its per-material batches now. */
    objBuildSectors(&g->level);

    g->entities = (EntityList *)malloc(sizeof(EntityList));
    entListInit(g->entities);
    entLoadFile(g->entities, levelEnt, &g->texCache, assetReg);

    g->hasDynLm = 0;   /* no runtime flashlight lightmap in the editor */
    return 1;
}

static void editFreeLevel(Game *g)
{
    if (g->entities) {
        entListFree(g->entities);
        free(g->entities);
        g->entities = NULL;
    }
    texCacheFree(&g->texCache);
    objFree(&g->level);
    /* No physCleanup / dynLmFree: the editor never initialised them. */
}

#endif /* EDIT_LOAD_H */
