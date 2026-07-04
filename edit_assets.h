#ifndef EDIT_ASSETS_H
#define EDIT_ASSETS_H

/* ---- Editor asset-manifest loader (header-only, static) ----
 *
 * The game populates the AssetRegistry via scriptLoadAssets (SOOB-Core
 * script.h), which needs a full ScriptSystem (UI/audio/music) to also register
 * the manifest's sounds/fonts. The editor only needs geometry: entLoadFile
 * resolves both mesh= and iqm= through assetRegResolveModel, and tex= through
 * assetRegResolveTexture — i.e. just the `models` and `textures` tables.
 *
 * So this runs assets.lua in a BARE Lua state and registers only those two
 * tables, keeping the editor free of the UI/audio/script runtime. It adds a
 * Lua dependency (links lua.o; needs -I.../vendor/lua-5.1.5/src) but nothing
 * else. Without it the registry is empty and .ent names fall through as raw
 * paths ("iqm: cannot open mrfixit"), so entities load without meshes.
 *
 * Include after asset_registry.h (AssetRegistry, assetRegAdd*) and a
 * forward-declared conLogf.
 */

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

/* Walk one `name = "path"` subtable of the manifest at the stack top,
   registering each pair via `add`. Mirrors SOOB-Core's scrWalkStringTable:
   the key is duplicated before lua_tostring so converting it doesn't confuse
   lua_next (which relies on the original key on the stack). */
static int editWalkAssetTable(lua_State *L, AssetRegistry *reg, const char *field,
                              int (*add)(AssetRegistry *, const char *, const char *))
{
    lua_getfield(L, -1, field);
    int count = 0;
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            lua_pushvalue(L, -2);                 /* dup key */
            const char *name = lua_tostring(L, -1);
            const char *path = lua_isstring(L, -2) ? lua_tostring(L, -2) : NULL;
            if (name && path) { add(reg, name, path); count++; }
            lua_pop(L, 2);                        /* key copy + value */
        }
    }
    lua_pop(L, 1);                                /* the subtable (or nil) */
    return count;
}

/* Populate `reg` with the models + textures from a manifest (assets.lua).
   Returns 1 on success, 0 if the file couldn't be loaded or run. */
static int editLoadAssets(AssetRegistry *reg, const char *path)
{
    lua_State *L = luaL_newstate();
    if (!L) { conLogf("edit: luaL_newstate failed\n"); return 0; }
    luaL_openlibs(L);

    if (luaL_loadfile(L, path) != 0 || lua_pcall(L, 0, 1, 0) != 0) {
        conLogf("edit: load %s: %s\n", path, lua_tostring(L, -1));
        lua_close(L);
        return 0;
    }
    if (!lua_istable(L, -1)) {
        conLogf("edit: %s must return a table\n", path);
        lua_close(L);
        return 0;
    }

    int models   = editWalkAssetTable(L, reg, "models",   assetRegAddModel);
    int textures = editWalkAssetTable(L, reg, "textures", assetRegAddTexture);
    conLogf("edit: %d model(s), %d texture(s) registered from %s\n",
            models, textures, path);

    lua_close(L);
    return 1;
}

#endif /* EDIT_ASSETS_H */
