#ifndef APP_EXT_H
#define APP_EXT_H

/*
 * SDLFun-specific Lua bindings layered on top of menu.h's AppState.
 * Splits from script_ext.h because these bindings need the full
 * AppState definition, which lives in menu.h — included AFTER game.h
 * (which in turn references script_ext.h's scriptExtSetEntities).
 *
 * Bindings:
 *   app_new_game()        -> sets pendingAction = PENDING_NEW_GAME
 *   app_continue()        -> sets pendingAction = PENDING_CONTINUE
 *                            (no-op if no game session yet)
 *   app_quit()            -> sets pendingAction = PENDING_QUIT
 *   app_has_game() -> bool  whether a game session has been created
 *                          and is still alive (drives CONTINUE enable)
 *   app_enter_menu()      -> flips mode back to MODE_MENU (Lua side
 *                            usually drives this implicitly by Esc
 *                            from within the game; provided for
 *                            symmetry with the C transition)
 *
 * Include AFTER menu.h. main.cpp wires the AppState pointer via
 * appExtSetApp(&app) after appInit completes.
 */

static AppState *g_appExt_app = NULL;

static int scr_app_new_game(lua_State *L)
{
    (void)L;
    if (g_appExt_app) g_appExt_app->pendingAction = PENDING_NEW_GAME;
    return 0;
}

static int scr_app_continue(lua_State *L)
{
    (void)L;
    if (g_appExt_app && g_appExt_app->game) {
        g_appExt_app->pendingAction = PENDING_CONTINUE;
    } else if (g_appExt_app) {
        conLogf("app_continue: no game session to resume — ignored\n");
    }
    return 0;
}

static int scr_app_quit(lua_State *L)
{
    (void)L;
    if (g_appExt_app) g_appExt_app->pendingAction = PENDING_QUIT;
    return 0;
}

static int scr_app_has_game(lua_State *L)
{
    lua_pushboolean(L, g_appExt_app && g_appExt_app->game != NULL);
    return 1;
}

static int scr_app_enter_menu(lua_State *L)
{
    (void)L;
    if (g_appExt_app) g_appExt_app->mode = MODE_MENU;
    return 0;
}

/* Register the app bindings on the ScriptSystem. Call once after
   scriptInit (and after script_ext.h's scriptExtRegister). */
static void appExtRegister(ScriptSystem *s)
{
    lua_register(s->L, "app_new_game",  scr_app_new_game);
    lua_register(s->L, "app_continue",  scr_app_continue);
    lua_register(s->L, "app_quit",      scr_app_quit);
    lua_register(s->L, "app_has_game",  scr_app_has_game);
    lua_register(s->L, "app_enter_menu",scr_app_enter_menu);
}

/* Set / clear the AppState pointer the bindings reach into.
   main.cpp calls appExtSetApp(&app) once after appInit. */
static void appExtSetApp(AppState *app)
{
    g_appExt_app = app;
}

#endif /* APP_EXT_H */
