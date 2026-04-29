#ifndef SCRIPT_H
#define SCRIPT_H

/*
 * Lua 5.1 scripting glue (header-only, static functions — same style as
 * the other engine modules).
 *
 * Holds the lua_State and borrowed pointers to UiState, SoundSystem,
 * SoundLibrary and EntityList so C bindings can reach engine state.
 *
 * v1 surface (per PLAN_LUA.md):
 *   ui_show_message(text [, seconds])   -> UiState transient message
 *   snd_play(name)                      -> SoundLibrary + SoundSystem
 *   ent_activate(target)                -> entActivate by name or group
 *   music_play(name [, fade [, loop]])  -> MusicLibrary + MusicSystem
 *   music_stop([fade])
 *   music_volume(g)
 *
 * Entry points into Lua are scriptCall()'d nullary globals — v1 only
 * fires on_start() once after level load. No per-frame or trigger hooks
 * yet; those land once the binding is proven.
 *
 * This header must be included after sound.h, music.h, ui.h, and entity.h.
 */

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "asset_registry.h"
#include "console.h"

struct ScriptSystem {
    lua_State     *L;
    UiState       *ui;
    SoundSystem   *snd;
    SoundLibrary  *sndLib;
    MusicSystem   *music;
    MusicLibrary  *musLib;
    EntityList    *entities;
    AssetRegistry *assets;
};

/* ---- C bindings ---- */

/* ui_show_message(text [, seconds])  — seconds defaults to 3. */
static int scr_ui_show_message(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "sdlfun.sys");
    ScriptSystem *s = (ScriptSystem *)lua_touserdata(L, -1);
    lua_pop(L, 1);

    const char *text = luaL_checkstring(L, 1);
    float seconds    = (float)luaL_optnumber(L, 2, 3.0);
    uiShowMessage(s->ui, text, seconds);
    return 0;
}

/* snd_play(name)            — head-relative (2D), for player events / UI.
   snd_play(name, x, y, z)   — positioned in world space (3D).
   Silently warns and returns if the name isn't registered. */
static int scr_snd_play(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "sdlfun.sys");
    ScriptSystem *s = (ScriptSystem *)lua_touserdata(L, -1);
    lua_pop(L, 1);

    const char *name = luaL_checkstring(L, 1);
    SoundBuffer b = sndLibPick(s->sndLib, name);
    if (!b) {
        conLogf("snd_play: unknown sound '%s'\n", name);
        return 0;
    }
    if (lua_gettop(L) >= 4) {
        Vec3 p;
        p.x = (float)luaL_checknumber(L, 2);
        p.y = (float)luaL_checknumber(L, 3);
        p.z = (float)luaL_checknumber(L, 4);
        sndPlayAt(s->snd, b, p);
    } else {
        sndPlay(s->snd, b);
    }
    return 0;
}

/* music_play(name [, fadeSec [, loop]])
     fadeSec defaults to 0.5; loop defaults to true.
   Falls through to a raw path if the name isn't registered in musLib. */
static int scr_music_play(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "sdlfun.sys");
    ScriptSystem *s = (ScriptSystem *)lua_touserdata(L, -1);
    lua_pop(L, 1);

    const char *name = luaL_checkstring(L, 1);
    float fadeSec = (float)luaL_optnumber(L, 2, 0.5);
    /* Default loop=true. lua_toboolean treats nil as false, so we have to
       distinguish "not passed" from "passed false". */
    int loop = (lua_gettop(L) >= 3) ? lua_toboolean(L, 3) : 1;
    musicPlay(s->music, s->musLib, name, fadeSec, loop);
    return 0;
}

/* music_stop([fadeSec]) — fadeSec defaults to 0.5. */
static int scr_music_stop(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "sdlfun.sys");
    ScriptSystem *s = (ScriptSystem *)lua_touserdata(L, -1);
    lua_pop(L, 1);

    float fadeSec = (float)luaL_optnumber(L, 1, 0.5);
    musicStop(s->music, fadeSec);
    return 0;
}

/* music_volume(g) — clamped to [0,1] inside musicSetVolume. */
static int scr_music_volume(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "sdlfun.sys");
    ScriptSystem *s = (ScriptSystem *)lua_touserdata(L, -1);
    lua_pop(L, 1);

    float g = (float)luaL_checknumber(L, 1);
    musicSetVolume(s->music, g);
    return 0;
}

/* ent_activate(target) — matches by entity name OR group, cascades switch
   targets. Returns nothing in v1. */
static int scr_ent_activate(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "sdlfun.sys");
    ScriptSystem *s = (ScriptSystem *)lua_touserdata(L, -1);
    lua_pop(L, 1);

    const char *target = luaL_checkstring(L, 1);
    entActivate(s->entities, target);
    return 0;
}

/* ---- Sandboxing ----
 * Lua's default opens os/io/package — a bad script could delete files or
 * load arbitrary DLLs. We're not defending against hostile scripts (this
 * is a single-player game), but we don't want an authoring mistake to
 * wipe the user's home directory either. Nil out the risky names before
 * running any user code. */
static void scriptSandbox(lua_State *L)
{
    const char *banned[] = {
        "os", "io", "package",
        "require", "dofile", "loadfile", "load", "loadstring",
        "module",
        NULL
    };
    for (int i = 0; banned[i]; i++) {
        lua_pushnil(L);
        lua_setglobal(L, banned[i]);
    }
}

/* ---- Error reporting ---- */
static int scr_traceback(lua_State *L)
{
    /* Standard traceback handler from lua 5.1's lua.c. */
    if (!lua_isstring(L, 1)) return 1;
    lua_getfield(L, LUA_GLOBALSINDEX, "debug");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return 1; }
    lua_getfield(L, -1, "traceback");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return 1; }
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);
    return 1;
}

/* ---- Lifecycle ---- */

static int scriptInit(ScriptSystem *s, UiState *ui, SoundSystem *snd,
                      SoundLibrary *sndLib, MusicSystem *music,
                      MusicLibrary *musLib, EntityList *el, AssetRegistry *reg)
{
    s->ui       = ui;
    s->snd      = snd;
    s->sndLib   = sndLib;
    s->music    = music;
    s->musLib   = musLib;
    s->entities = el;
    s->assets   = reg;

    s->L = luaL_newstate();
    if (!s->L) {
        conLogf("script: luaL_newstate failed\n");
        return 0;
    }
    luaL_openlibs(s->L);
    scriptSandbox(s->L);

    /* Stash the system pointer in the Lua registry so bindings can reach it
       without relying on a C-side global. */
    lua_pushlightuserdata(s->L, s);
    lua_setfield(s->L, LUA_REGISTRYINDEX, "sdlfun.sys");

    /* Register bindings into the global table. */
    lua_register(s->L, "ui_show_message", scr_ui_show_message);
    lua_register(s->L, "snd_play",        scr_snd_play);
    lua_register(s->L, "music_play",      scr_music_play);
    lua_register(s->L, "music_stop",      scr_music_stop);
    lua_register(s->L, "music_volume",    scr_music_volume);
    lua_register(s->L, "ent_activate",    scr_ent_activate);

    conLogf("script: Lua %s initialised\n", LUA_VERSION);
    return 1;
}

static void scriptShutdown(ScriptSystem *s)
{
    if (s->L) { lua_close(s->L); s->L = NULL; }
}

/* Load and execute a file. Errors are printed to stderr and the function
   returns 0. Stack left clean either way. */
static int scriptRunFile(ScriptSystem *s, const char *path)
{
    lua_pushcfunction(s->L, scr_traceback);
    int tbidx = lua_gettop(s->L);

    if (luaL_loadfile(s->L, path) != 0) {
        conLogf("script: load %s: %s\n", path, lua_tostring(s->L, -1));
        lua_pop(s->L, 2);
        return 0;
    }
    if (lua_pcall(s->L, 0, 0, tbidx) != 0) {
        conLogf("script: run %s: %s\n", path, lua_tostring(s->L, -1));
        lua_pop(s->L, 2);
        return 0;
    }
    lua_pop(s->L, 1); /* traceback */
    return 1;
}

/* Walk a name=path subtable at stack top and dispatch each pair to
   `onPair`. Used by scriptLoadAssets for sounds / models / textures.
   Safely duplicates keys before lua_tostring per the Lua 5.1 docs:
   lua_tostring can mutate the value on the stack into a string, and
   mutating a key breaks the next lua_next. */
static int scr_walkStringTable(lua_State *L, ScriptSystem *s,
                               void (*onPair)(ScriptSystem *, const char *, const char *))
{
    if (!lua_istable(L, -1)) return 0;
    int count = 0;
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        /* stack: ... table, key, value */
        lua_pushvalue(L, -2);  /* copy key so lua_tostring can't corrupt it */
        const char *name = lua_tostring(L, -1);
        const char *val  = lua_isstring(L, -2) ? lua_tostring(L, -2) : NULL;
        if (name && val) { onPair(s, name, val); count++; }
        lua_pop(L, 2);  /* key-copy and value */
    }
    return count;
}

static void scr_onSound(ScriptSystem *s, const char *name, const char *path)
{
    sndLibAdd(s->sndLib, name, sndLoadWav(path));
}

/* Walk the manifest's `sounds` subtable. A value can be either a single
   path string (registers one buffer under `name`) or an array of paths
   (registers all of them as variants of the same group, picked randomly
   at play time). Anything else is skipped with a warning. */
static int scr_walkSoundsTable(lua_State *L, ScriptSystem *s)
{
    if (!lua_istable(L, -1)) return 0;
    int count = 0;
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        /* stack: ... soundsTable, key, value */
        lua_pushvalue(L, -2);  /* dup key (lua_tostring may mutate) */
        const char *name = lua_tostring(L, -1);

        if (name && lua_isstring(L, -2)) {
            scr_onSound(s, name, lua_tostring(L, -2));
            count++;
        } else if (name && lua_istable(L, -2)) {
            /* Iterate the variant array. Use ipairs-style sequential keys. */
            int n = (int)lua_objlen(L, -2);
            for (int i = 1; i <= n; i++) {
                lua_rawgeti(L, -2, i);
                if (lua_isstring(L, -1)) {
                    scr_onSound(s, name, lua_tostring(L, -1));
                }
                lua_pop(L, 1);
            }
            if (n > 0) count++;
        } else if (name) {
            conLogf("script: sounds.%s must be a path string or array of paths\n", name);
        }
        lua_pop(L, 2);  /* key-copy and value */
    }
    return count;
}
static void scr_onMusic(ScriptSystem *s, const char *name, const char *path)
{
    musicLibAdd(s->musLib, name, path);
}
static void scr_onModel(ScriptSystem *s, const char *name, const char *path)
{
    assetRegAddModel(s->assets, name, path);
}
static void scr_onTexture(ScriptSystem *s, const char *name, const char *path)
{
    assetRegAddTexture(s->assets, name, path);
}
static void scr_onFont(ScriptSystem *s, const char *name, const char *path)
{
    uiFontLoad(&s->ui->fonts, name, path);
}

/* Load the asset manifest (expected to `return` a table shaped like
   assets.lua). Walks manifest.sounds / manifest.models / manifest.textures
   as { name = path, ... } maps, registering each into the sound library
   or the AssetRegistry. Missing tables are fine — nothing gets registered. */
static int scriptLoadAssets(ScriptSystem *s, const char *path)
{
    lua_State *L = s->L;
    lua_pushcfunction(L, scr_traceback);
    int tbidx = lua_gettop(L);

    if (luaL_loadfile(L, path) != 0) {
        conLogf("script: load %s: %s\n", path, lua_tostring(L, -1));
        lua_pop(L, 2);
        return 0;
    }
    if (lua_pcall(L, 0, 1, tbidx) != 0) {
        conLogf("script: run %s: %s\n", path, lua_tostring(L, -1));
        lua_pop(L, 2);
        return 0;
    }
    if (!lua_istable(L, -1)) {
        conLogf("script: %s must return a table\n", path);
        lua_pop(L, 2);
        return 0;
    }

    lua_getfield(L, -1, "sounds");
    int sounds = scr_walkSoundsTable(L, s);
    lua_pop(L, 1);

    lua_getfield(L, -1, "music");
    int music = scr_walkStringTable(L, s, scr_onMusic);
    lua_pop(L, 1);

    lua_getfield(L, -1, "models");
    int models = scr_walkStringTable(L, s, scr_onModel);
    lua_pop(L, 1);

    lua_getfield(L, -1, "textures");
    int textures = scr_walkStringTable(L, s, scr_onTexture);
    lua_pop(L, 1);

    lua_getfield(L, -1, "fonts");
    int fonts = scr_walkStringTable(L, s, scr_onFont);
    lua_pop(L, 1);

    lua_pop(L, 2);  /* manifest, traceback */
    conLogf("assets: %d sound(s), %d music, %d model(s), %d texture(s), %d font(s) registered from %s\n",
           sounds, music, models, textures, fonts, path);
    return 1;
}

/* ---- Console integration ---- */

/* Lua `print` override — joins args with tabs (standard print behavior)
   and routes the resulting line through conLogf so the dev console shows
   Lua output. Also still writes to stdout via conLogf's tee. */
static int scr_print(lua_State *L)
{
    int n = lua_gettop(L);
    char buf[512];
    int pos = 0;
    lua_getglobal(L, "tostring");
    for (int i = 1; i <= n; i++) {
        lua_pushvalue(L, -1);   /* tostring */
        lua_pushvalue(L, i);
        lua_call(L, 1, 1);
        const char *s = lua_tostring(L, -1);
        if (!s) s = "(nil)";
        if (pos > 0 && pos < (int)sizeof(buf) - 1) buf[pos++] = '\t';
        while (*s && pos < (int)sizeof(buf) - 1) buf[pos++] = *s++;
        lua_pop(L, 1);          /* result of tostring */
    }
    lua_pop(L, 1);              /* tostring itself */
    buf[pos] = '\0';
    conLogf("%s\n", buf);
    return 0;
}

static void scriptInstallConsolePrint(ScriptSystem *s)
{
    lua_register(s->L, "print", scr_print);
}

/* Execute the console's command buffer as a Lua chunk. Caller clears the
   command afterwards. Compile and runtime errors are formatted and pushed
   to the scrollback; successful calls with no output just complete
   silently (use print() to see values). */
static void conExecute(Console *c, ScriptSystem *s)
{
    conLogf("> %s\n", c->cmd);
    lua_State *L = s->L;
    lua_pushcfunction(L, scr_traceback);
    int tbidx = lua_gettop(L);
    if (luaL_loadstring(L, c->cmd) != 0) {
        conLogf("%s\n", lua_tostring(L, -1));
        lua_pop(L, 2);   /* error + traceback */
        return;
    }
    if (lua_pcall(L, 0, 0, tbidx) != 0) {
        conLogf("%s\n", lua_tostring(L, -1));
        lua_pop(L, 2);
        return;
    }
    lua_pop(L, 1);       /* traceback */
}

/* Call a nullary global function if it exists. Missing function is a
   no-op, not an error — scripts are free to define only the hooks they
   care about. */
static int scriptCall(ScriptSystem *s, const char *fn)
{
    lua_pushcfunction(s->L, scr_traceback);
    int tbidx = lua_gettop(s->L);

    lua_getglobal(s->L, fn);
    if (!lua_isfunction(s->L, -1)) {
        lua_pop(s->L, 2);
        return 0;
    }
    if (lua_pcall(s->L, 0, 0, tbidx) != 0) {
        conLogf("script: %s(): %s\n", fn, lua_tostring(s->L, -1));
        lua_pop(s->L, 2);
        return 0;
    }
    lua_pop(s->L, 1); /* traceback */
    return 1;
}

#endif /* SCRIPT_H */
