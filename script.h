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
 *
 * Entry points into Lua are scriptCall()'d nullary globals — v1 only
 * fires on_start() once after level load. No per-frame or trigger hooks
 * yet; those land once the binding is proven.
 *
 * This header must be included after sound.h, ui.h, and entity.h.
 */

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

struct ScriptSystem {
    lua_State    *L;
    UiState      *ui;
    SoundSystem  *snd;
    SoundLibrary *sndLib;
    EntityList   *entities;
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

/* snd_play(name) — silently warns and returns if the name isn't registered. */
static int scr_snd_play(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "sdlfun.sys");
    ScriptSystem *s = (ScriptSystem *)lua_touserdata(L, -1);
    lua_pop(L, 1);

    const char *name = luaL_checkstring(L, 1);
    SoundBuffer b = sndLibFind(s->sndLib, name);
    if (!b) {
        fprintf(stderr, "snd_play: unknown sound '%s'\n", name);
        return 0;
    }
    sndPlay(s->snd, b);
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
                      SoundLibrary *sndLib, EntityList *el)
{
    s->ui       = ui;
    s->snd      = snd;
    s->sndLib   = sndLib;
    s->entities = el;

    s->L = luaL_newstate();
    if (!s->L) {
        fprintf(stderr, "script: luaL_newstate failed\n");
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
    lua_register(s->L, "ent_activate",    scr_ent_activate);

    printf("script: Lua %s initialised\n", LUA_VERSION);
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
        fprintf(stderr, "script: load %s: %s\n", path, lua_tostring(s->L, -1));
        lua_pop(s->L, 2);
        return 0;
    }
    if (lua_pcall(s->L, 0, 0, tbidx) != 0) {
        fprintf(stderr, "script: run %s: %s\n", path, lua_tostring(s->L, -1));
        lua_pop(s->L, 2);
        return 0;
    }
    lua_pop(s->L, 1); /* traceback */
    return 1;
}

/* Load the asset manifest (expected to `return` a table shaped like
   assets.lua). Walks manifest.sounds = { name = path, ... }, loading
   each WAV into the SoundLibrary. Missing or non-table `sounds` is
   fine — just means no sounds get registered.

   Safely duplicates keys before lua_tostring per the Lua 5.1 docs:
   lua_tostring can mutate the value on the stack into a string, and
   mutating a key breaks the next lua_next. */
static int scriptLoadAssets(ScriptSystem *s, const char *path)
{
    lua_State *L = s->L;
    lua_pushcfunction(L, scr_traceback);
    int tbidx = lua_gettop(L);

    if (luaL_loadfile(L, path) != 0) {
        fprintf(stderr, "script: load %s: %s\n", path, lua_tostring(L, -1));
        lua_pop(L, 2);
        return 0;
    }
    if (lua_pcall(L, 0, 1, tbidx) != 0) {
        fprintf(stderr, "script: run %s: %s\n", path, lua_tostring(L, -1));
        lua_pop(L, 2);
        return 0;
    }
    if (!lua_istable(L, -1)) {
        fprintf(stderr, "script: %s must return a table\n", path);
        lua_pop(L, 2);
        return 0;
    }

    int loaded = 0;
    lua_getfield(L, -1, "sounds");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            /* stack: ... manifest, sounds, key, value */
            lua_pushvalue(L, -2);  /* copy key so lua_tostring can't corrupt it */
            const char *name = lua_tostring(L, -1);
            const char *wav  = lua_isstring(L, -2) ? lua_tostring(L, -2) : NULL;
            if (name && wav) {
                sndLibRegister(s->sndLib, name, sndLoadWav(wav));
                loaded++;
            }
            lua_pop(L, 2);  /* pop key-copy and value, leave original key */
        }
    }
    lua_pop(L, 1);  /* sounds (or non-table) */
    lua_pop(L, 2);  /* manifest, traceback */
    printf("assets: %d sound(s) registered from %s\n", loaded, path);
    return 1;
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
        fprintf(stderr, "script: %s(): %s\n", fn, lua_tostring(s->L, -1));
        lua_pop(s->L, 2);
        return 0;
    }
    lua_pop(s->L, 1); /* traceback */
    return 1;
}

#endif /* SCRIPT_H */
