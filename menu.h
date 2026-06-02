#ifndef MENU_H
#define MENU_H

/*
 * App-level state container. The screen / button / dialog rendering
 * that used to live here moved to scripts/menu.lua (Lua scenes on top
 * of engine.scene). C still owns AppState, the per-mode flag, and the
 * pendingAction one-shot — main.cpp processes pendingAction once per
 * frame and runs the gameInit / gameFree / mode-switch side-effects.
 *
 * What stays in this header:
 *   - AppState struct + AppMode enum
 *   - PENDING_* action codes (set by Lua via app_* bindings, processed
 *     by main.cpp)
 *   - appInit / appShutdown lifecycle
 *   - appEnterMenu helper (flips mode from MODE_GAME back to MODE_MENU)
 *   - drawLoadingScreen (one-shot frame during gameInit's blocking load)
 *
 * Must be included after ui.h, texture.h, asset_registry.h, script.h.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

struct Game;  /* forward; AppState only holds a pointer */

/* Actions queued by Lua's app_* bindings; consumed by main.cpp once
   per frame to run game-state side effects. */
#define PENDING_NONE      0
#define PENDING_NEW_GAME  1
#define PENDING_CONTINUE  2
#define PENDING_QUIT      3

typedef enum { MODE_MENU, MODE_GAME } AppMode;

struct AppState {
    AppMode mode;
    int     running;
    int     screenW, screenH;

    /* NULL until first New Game; preserved when player Escapes out of a
       running game so Continue can come back to it. main.cpp owns the
       Game struct and wires this pointer during init. */
    Game   *game;

    /* Cache of menu-scope textures (menu_bg, logo, dialog_bg, loading_bg).
       Kept separate from any Game::texCache so menu textures survive
       gameInit/gameFree cycles. Also shared with the Lua side as the
       texCache passed to scriptInit. */
    TexCache menuTex;

    /* Borrowed pointers to app-scope subsystems. */
    UiState       *ui;
    AssetRegistry *assetReg;
    ScriptSystem  *script;
    SoundSystem   *snd;
    SoundLibrary  *sndLib;
    MusicSystem   *music;
    MusicLibrary  *musLib;

    /* One-shot action consumed by main.cpp each frame. Lua's app_*
       bindings (app_ext.h) set it; main.cpp resets to PENDING_NONE
       after handling. */
    int pendingAction;
};

/* ---- App lifecycle --------------------------------------------------- */

static void appInit(AppState *app, int screenW, int screenH,
                    UiState *ui, AssetRegistry *ar, ScriptSystem *script,
                    SoundSystem *snd, SoundLibrary *sndLib,
                    MusicSystem *music, MusicLibrary *musLib)
{
    memset(app, 0, sizeof(*app));
    app->mode    = MODE_MENU;
    app->running = 1;
    app->screenW = screenW;
    app->screenH = screenH;
    app->ui       = ui;
    app->assetReg = ar;
    app->script   = script;
    app->snd      = snd;
    app->sndLib   = sndLib;
    app->music    = music;
    app->musLib   = musLib;
    texCacheInit(&app->menuTex);
    /* Title music is now started by Lua's mainMenu:enter() — no need
       to call musicPlay here. The Lua side has the asset name and the
       binding. */
}

static void appShutdown(AppState *app)
{
    texCacheFree(&app->menuTex);
}

/* Transition from MODE_GAME back to MODE_MENU. The Lua scene stack
   stays intact across the mode flip — the main menu sits at the bottom
   throughout — so this is just a mode toggle. Lua's mainMenu picks up
   the new appHasGame() state on its next render. */
static void appEnterMenu(AppState *app)
{
    app->mode = MODE_MENU;
}

/* ---- Loading splash --------------------------------------------------
 *
 * Called from main.cpp right before a (synchronous) gameInit so the
 * user sees something while the level loads. Does its own clear +
 * uiBegin/uiEnd + SwapBuffers since it's fired outside the regular
 * frame loop. Uses the menuTex cache (shared with the Lua menu) so
 * the loading_bg texture is decoded once and reused.
 */
static GLuint appGetMenuTex(AppState *app, const char *name, int wrapMode)
{
    const char *path = assetRegFindTexture(app->assetReg, name);
    if (!path) return 0;
    return texCacheGet(&app->menuTex, path, wrapMode);
}

static void drawLoadingScreen(AppState *app)
{
    UiState *ui = app->ui;
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    uiBegin(ui);

    /* Cover-fit loading_bg. Assumes a 2:1 source aspect (256x128 PNG).
       If the texture isn't registered, falls through to a dark fill so
       the LOADING label still has somewhere to land. */
    float vw = uiGetWidth(ui);
    float vh = uiGetHeight(ui);
    UiRect full = uiRectMake(-vw * 0.5f, -vh * 0.5f, vw, vh);
    GLuint tex = appGetMenuTex(app, "loading_bg", GL_CLAMP_TO_EDGE);
    if (tex) {
        float srcAspect = 2.0f / 1.0f;
        float dstAspect = vw / vh;
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        if (dstAspect > srcAspect) {
            float visible = srcAspect / dstAspect;
            float crop = (1.0f - visible) * 0.5f;
            v0 = crop; v1 = 1.0f - crop;
        } else {
            float visible = dstAspect / srcAspect;
            float crop = (1.0f - visible) * 0.5f;
            u0 = crop; u1 = 1.0f - crop;
        }
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex);
        glColor4f(1, 1, 1, 1);
        glBegin(GL_QUADS);
            glTexCoord2f(u0, v0); glVertex2f(full.x,           full.y);
            glTexCoord2f(u1, v0); glVertex2f(full.x + full.w,  full.y);
            glTexCoord2f(u1, v1); glVertex2f(full.x + full.w,  full.y + full.h);
            glTexCoord2f(u0, v1); glVertex2f(full.x,           full.y + full.h);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    } else {
        uiQuad(full, uiRgba(0.04f, 0.03f, 0.08f, 1.0f));
    }

    float halfW = uiGetWidth(ui)  * 0.5f;
    float halfH = uiGetHeight(ui) * 0.5f;
    UiColor fg = uiRgba(1, 1, 1, 1);
    /* button_font = orbitron_small alias, lineHeight 28. Old scale 5.0
       targeted ~40 vpx; in the new convention that's 40/28. */
    uiText(ui, halfW - 30.0f, halfH - 30.0f,
           fg, "LOADING", 40.0f / 28.0f,
           UI_ALIGN_BOTTOM | UI_ALIGN_RIGHT, "button_font");
    uiEnd(ui);

    SDL_GL_SwapBuffers();
}

#endif /* MENU_H */
