#ifndef MENU_H
#define MENU_H

/*
 * Screen / menu system. A lightweight ScreenStack sits on top of the game.
 * When AppState::mode == MODE_MENU, input and rendering route through the
 * top screen (with lower screens drawn beneath as context — e.g. a Dialog
 * renders a dim overlay over the MainMenu underneath it). In MODE_GAME
 * the stack is unused and the game loop runs normally.
 *
 * Screens are tagged unions (matching the Entity convention used elsewhere)
 * so all three screen types can share a flat array without virtual calls.
 *
 * Menu actions that require game-state side effects (New Game → gameInit,
 * Continue → mode transition, Exit → quit) are not executed inside menu.h.
 * Screen handlers set AppState::pendingAction; main.cpp processes it once
 * per frame and owns all gameInit/gameFree/mode-switch logic. This keeps
 * menu.h free of Game internals — it only needs the forward declaration.
 *
 * Must be included after ui.h, texture.h, and asset_registry.h.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

struct Game;  /* forward; menu.h never dereferences Game directly */

/* ---- Screen tagged-union ---- */

typedef enum {
    SCREEN_MAIN_MENU,
    SCREEN_OPTIONS,
    SCREEN_DIALOG
} ScreenType;

/* Actions fired by screen handlers. Most are processed in-menu (push/pop
   screens, move focus); a few bubble up to main.cpp via pendingAction. */
#define MENU_ACTION_NONE         0
#define MENU_ACTION_CONTINUE     1
#define MENU_ACTION_NEW_GAME     2
#define MENU_ACTION_OPTIONS      3
#define MENU_ACTION_EXIT         4
#define MENU_ACTION_BACK         5

/* Dialog OK-confirm codes — what the dialog does when its OK is chosen. */
#define DLG_CONFIRM_NONE     0
#define DLG_CONFIRM_QUIT     1
#define DLG_CONFIRM_NEW_GAME 2

/* Actions that bubble up to main.cpp for game-state side effects. Others
   are handled inside menu.h without touching app->pendingAction. */
#define PENDING_NONE         0
#define PENDING_NEW_GAME     1
#define PENDING_CONTINUE     2
#define PENDING_QUIT         3

#define MENU_LABEL_MAX 32

struct MenuButton {
    char   label[MENU_LABEL_MAX];
    UiRect rect;          /* virtual-canvas coords */
    int    enabled;
    int    action;        /* MENU_ACTION_* */
};

#define MAIN_MENU_MAX 8
struct MainMenu {
    MenuButton buttons[MAIN_MENU_MAX];
    int        count;
    int        focused;
};

struct Options {
    MenuButton back;
    int        focused;   /* always 0 for v1 */
};

struct Dialog {
    char title[128];
    char message[256];
    int  focused;          /* 0=OK, 1=Cancel (default) */
    int  okConfirm;        /* DLG_CONFIRM_* */
};

struct Screen {
    ScreenType type;
    union {
        MainMenu mm;
        Options  opt;
        Dialog   dlg;
    };
};

#define SCREEN_STACK_MAX 4
struct ScreenStack {
    Screen items[SCREEN_STACK_MAX];
    int    count;
};

/* ---- AppState ---- */

typedef enum { MODE_MENU, MODE_GAME } AppMode;

struct AppState {
    AppMode mode;
    int     running;
    int     screenW, screenH;

    /* NULL until first New Game; preserved when player Escapes out of a
       running game so Continue can come back to it. main.cpp owns the
       Game struct and wires this pointer during init. */
    Game   *game;

    /* Cache of menu-scope textures (menu_bg, dialog_bg, logo). Kept
       separate from any Game::texCache so menu textures survive
       gameInit/gameFree cycles. */
    TexCache    menuTex;
    ScreenStack screens;

    /* Borrowed pointers to app-scope subsystems. */
    UiState       *ui;
    AssetRegistry *assetReg;
    ScriptSystem  *script;
    SoundSystem   *snd;
    SoundLibrary  *sndLib;

    /* One-shot action consumed by main.cpp each frame. */
    int pendingAction;
};

/* ---- Stack helpers ---- */

static Screen *screenStackTop(ScreenStack *st)
{
    if (st->count <= 0) return NULL;
    return &st->items[st->count - 1];
}

static int screenStackPush(ScreenStack *st, Screen *s)
{
    if (st->count >= SCREEN_STACK_MAX) return 0;
    st->items[st->count++] = *s;
    return 1;
}

static void screenStackPop(ScreenStack *st)
{
    if (st->count > 0) st->count--;
}

static void screenStackClear(ScreenStack *st) { st->count = 0; }

/* ---- Menu layout constants ---- */

#define MENU_PADDING      60.0f
#define MENU_BTN_W        380.0f
#define MENU_BTN_H        56.0f
#define MENU_BTN_GAP      14.0f
#define MENU_LOGO_H       96.0f   /* virtual-px tall, aspect preserved */
#define MENU_DIALOG_W     720.0f
#define MENU_DIALOG_H     300.0f
#define MENU_DLG_BTN_W    200.0f
#define MENU_DLG_BTN_H    56.0f
#define MENU_DLG_PAD      40.0f

/* ---- Screen constructors ---- */

static void menuButtonSet(MenuButton *b, const char *label, int action,
                          float x, float y, float w, float h, int enabled)
{
    strncpy(b->label, label, MENU_LABEL_MAX - 1);
    b->label[MENU_LABEL_MAX - 1] = '\0';
    b->action  = action;
    b->enabled = enabled;
    b->rect.x = x; b->rect.y = y; b->rect.w = w; b->rect.h = h;
}

/* Lay out the main menu's buttons top-left with padding, keyed off the
   current virtual canvas size. Called on init and whenever the menu is
   (re-)pushed so "Continue" reflects whether a game is running. */
static void mainMenuLayout(MainMenu *mm, UiState *ui, int haveGame)
{
    const float halfW = uiGetWidth(ui)  * 0.5f;
    const float halfH = uiGetHeight(ui) * 0.5f;

    /* Top-left anchor for the button column: under the logo. */
    float x = -halfW + MENU_PADDING;
    float y = -halfH + MENU_PADDING + MENU_LOGO_H + MENU_PADDING;

    mm->count = 0;
    menuButtonSet(&mm->buttons[mm->count++], "CONTINUE", MENU_ACTION_CONTINUE,
                  x, y, MENU_BTN_W, MENU_BTN_H, haveGame);
    y += MENU_BTN_H + MENU_BTN_GAP;
    menuButtonSet(&mm->buttons[mm->count++], "NEW GAME", MENU_ACTION_NEW_GAME,
                  x, y, MENU_BTN_W, MENU_BTN_H, 1);
    y += MENU_BTN_H + MENU_BTN_GAP;
    menuButtonSet(&mm->buttons[mm->count++], "OPTIONS", MENU_ACTION_OPTIONS,
                  x, y, MENU_BTN_W, MENU_BTN_H, 1);
    y += MENU_BTN_H + MENU_BTN_GAP;
    menuButtonSet(&mm->buttons[mm->count++], "EXIT", MENU_ACTION_EXIT,
                  x, y, MENU_BTN_W, MENU_BTN_H, 1);

    /* Ensure focus sits on an enabled button. */
    if (mm->focused < 0 || mm->focused >= mm->count ||
        !mm->buttons[mm->focused].enabled) {
        for (int i = 0; i < mm->count; i++) {
            if (mm->buttons[i].enabled) { mm->focused = i; break; }
        }
    }
}

static void optionsLayout(Options *o, UiState *ui)
{
    float w = MENU_BTN_W, h = MENU_BTN_H;
    menuButtonSet(&o->back, "BACK", MENU_ACTION_BACK,
                  -w * 0.5f, -h * 0.5f, w, h, 1);
    (void)ui;
    o->focused = 0;
}

static Screen makeMainMenu(UiState *ui, int haveGame)
{
    Screen s; s.type = SCREEN_MAIN_MENU;
    s.mm.count = 0;
    s.mm.focused = 0;
    mainMenuLayout(&s.mm, ui, haveGame);
    return s;
}

static Screen makeOptions(UiState *ui)
{
    Screen s; s.type = SCREEN_OPTIONS;
    optionsLayout(&s.opt, ui);
    return s;
}

static Screen makeDialog(const char *title, const char *message, int okConfirm)
{
    Screen s; s.type = SCREEN_DIALOG;
    strncpy(s.dlg.title, title, sizeof(s.dlg.title) - 1);
    s.dlg.title[sizeof(s.dlg.title) - 1] = '\0';
    strncpy(s.dlg.message, message, sizeof(s.dlg.message) - 1);
    s.dlg.message[sizeof(s.dlg.message) - 1] = '\0';
    s.dlg.focused   = 1;            /* Cancel is default focus */
    s.dlg.okConfirm = okConfirm;
    return s;
}

/* ---- Rendering helpers ---- */

/* Load a texture by logical name via the asset registry into the menu
   texture cache. Returns 0 if not found. */
static GLuint appGetMenuTex(AppState *app, const char *name, int wrapMode)
{
    const char *path = assetRegFindTexture(app->assetReg, name);
    if (!path) return 0;
    return texCacheGet(&app->menuTex, path, wrapMode);
}

/* Draw a texture as a cover-fit quad (fills the whole virtual canvas,
   crops on whichever axis has excess, keeps aspect ratio). If the texture
   isn't loaded, draws a flat fallback color so something is still visible.
   Assumes a 16:9 source aspect — the .bmp loader doesn't surface size
   through the TexCache API, and the menu/loading art we ship is landscape. */
static void menuDrawCoverBg(AppState *app, const char *name, UiColor fallback)
{
    UiState *ui = app->ui;
    float vw = uiGetWidth(ui);
    float vh = uiGetHeight(ui);
    UiRect full = uiRectMake(-vw * 0.5f, -vh * 0.5f, vw, vh);

    GLuint tex = appGetMenuTex(app, name, GL_CLAMP_TO_EDGE);
    if (!tex) { uiQuad(full, fallback); return; }

    float srcAspect = 16.0f / 9.0f;
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
}

static void menuDrawBackground(AppState *app)
{
    menuDrawCoverBg(app, "menu_bg", uiRgb(0.08f, 0.08f, 0.16f));
}

static void menuDrawLogo(AppState *app)
{
    UiState *ui = app->ui;
    GLuint tex = appGetMenuTex(app, "logo", GL_CLAMP_TO_EDGE);
    if (!tex) return;
    /* Assume logo source is 256x64 (4:1 aspect) — the placeholder art we
       ship uses those dimensions. Real logo art should keep the 4:1 ratio
       or this layout needs updating. */
    float h = MENU_LOGO_H;
    float w = h * (256.0f / 64.0f);
    float halfW = uiGetWidth(ui)  * 0.5f;
    float halfH = uiGetHeight(ui) * 0.5f;
    UiRect r = uiRectMake(-halfW + MENU_PADDING, -halfH + MENU_PADDING, w, h);
    uiIcon(r, tex);
}

static void menuDrawButton(UiState *ui, const MenuButton *b, int focused)
{
    UiColor bg, fg;
    if (!b->enabled) {
        bg = uiRgba(0.1f, 0.1f, 0.14f, 0.75f);
        fg = uiRgba(0.55f, 0.55f, 0.60f, 1.0f);
    } else if (focused) {
        bg = uiRgba(0.55f, 0.25f, 0.85f, 0.85f);
        fg = uiRgba(1.0f,  1.0f,  1.0f,  1.0f);
    } else {
        bg = uiRgba(0.14f, 0.14f, 0.22f, 0.75f);
        fg = uiRgba(0.90f, 0.90f, 0.95f, 1.0f);
    }
    uiQuad(b->rect, bg);
    /* Text anchored at vertical middle of the button, left-padded. */
    float tx = b->rect.x + 24.0f;
    float ty = b->rect.y + b->rect.h * 0.5f;
    uiText(ui, tx, ty, fg, b->label, 3.0f,
           UI_ALIGN_MIDDLE | UI_ALIGN_LEFT, "button_font");
}

/* ---- Main menu dispatch ---- */

static void mainMenuKey(MainMenu *mm, SDLKey sym, AppState *app)
{
    if (sym == SDLK_UP) {
        int i = mm->focused;
        for (int n = 0; n < mm->count; n++) {
            i = (i - 1 + mm->count) % mm->count;
            if (mm->buttons[i].enabled) { mm->focused = i; break; }
        }
        return;
    }
    if (sym == SDLK_DOWN) {
        int i = mm->focused;
        for (int n = 0; n < mm->count; n++) {
            i = (i + 1) % mm->count;
            if (mm->buttons[i].enabled) { mm->focused = i; break; }
        }
        return;
    }
    if (sym == SDLK_RETURN || sym == SDLK_SPACE) {
        if (mm->focused < 0 || mm->focused >= mm->count) return;
        MenuButton *b = &mm->buttons[mm->focused];
        if (!b->enabled) return;
        switch (b->action) {
            case MENU_ACTION_CONTINUE:
                app->pendingAction = PENDING_CONTINUE;
                break;
            case MENU_ACTION_NEW_GAME:
                if (app->game) {
                    /* Prior session exists — confirm before discarding. */
                    Screen s = makeDialog("NEW GAME",
                                          "Start a new game? Progress will be lost.",
                                          DLG_CONFIRM_NEW_GAME);
                    screenStackPush(&app->screens, &s);
                } else {
                    app->pendingAction = PENDING_NEW_GAME;
                }
                break;
            case MENU_ACTION_OPTIONS: {
                Screen s = makeOptions(app->ui);
                screenStackPush(&app->screens, &s);
                break;
            }
            case MENU_ACTION_EXIT: {
                Screen s = makeDialog("EXIT", "Exit to system?", DLG_CONFIRM_QUIT);
                screenStackPush(&app->screens, &s);
                break;
            }
        }
    }
}

static void mainMenuMouse(MainMenu *mm, float vx, float vy, int clicked, AppState *app)
{
    for (int i = 0; i < mm->count; i++) {
        MenuButton *b = &mm->buttons[i];
        if (!uiRectContains(b->rect, vx, vy)) continue;
        if (b->enabled) mm->focused = i;
        if (clicked && b->enabled) {
            /* Reuse key dispatch by simulating Enter on the focused button. */
            mainMenuKey(mm, SDLK_RETURN, app);
        }
        return;
    }
}

static void mainMenuRender(MainMenu *mm, AppState *app)
{
    menuDrawBackground(app);
    menuDrawLogo(app);
    for (int i = 0; i < mm->count; i++) {
        menuDrawButton(app->ui, &mm->buttons[i], i == mm->focused);
    }
}

/* ---- Options dispatch ---- */

static void optionsKey(Options *o, SDLKey sym, AppState *app)
{
    (void)o;
    if (sym == SDLK_RETURN || sym == SDLK_SPACE ||
        sym == SDLK_ESCAPE || sym == SDLK_BACKSPACE) {
        screenStackPop(&app->screens);
    }
}

static void optionsMouse(Options *o, float vx, float vy, int clicked, AppState *app)
{
    if (uiRectContains(o->back.rect, vx, vy)) {
        o->focused = 0;
        if (clicked) screenStackPop(&app->screens);
    }
}

static void optionsRender(Options *o, AppState *app)
{
    /* Background: keep the main menu visible underneath — drawn by the
       menuTick loop before us. */
    UiState *ui = app->ui;
    float halfW = uiGetWidth(ui)  * 0.5f;
    float halfH = uiGetHeight(ui) * 0.5f;

    /* Dim the main menu so Options reads as a distinct panel. */
    uiQuad(uiRectMake(-halfW, -halfH, uiGetWidth(ui), uiGetHeight(ui)),
           uiRgba(0, 0, 0, 0.5f));

    UiColor title = uiRgba(1, 1, 1, 1);
    uiText(ui, 0.0f, -halfH + MENU_PADDING, title, "OPTIONS",
           5.0f, UI_ALIGN_TOP | UI_ALIGN_CENTER, "menu_title_font");

    menuDrawButton(ui, &o->back, o->focused == 0);
}

/* ---- Dialog dispatch ---- */

static UiRect dialogRect(UiState *ui)
{
    float w = MENU_DIALOG_W, h = MENU_DIALOG_H;
    return uiRectMake(-w * 0.5f, -h * 0.5f, w, h);
}

static UiRect dialogBtnRect(UiState *ui, int which /* 0=OK, 1=Cancel */)
{
    UiRect r = dialogRect(ui);
    float bw = MENU_DLG_BTN_W, bh = MENU_DLG_BTN_H;
    float gap = 20.0f;
    float totalW = bw * 2 + gap;
    float bx = r.x + (r.w - totalW) * 0.5f + (which == 0 ? 0 : bw + gap);
    float by = r.y + r.h - MENU_DLG_PAD - bh;
    return uiRectMake(bx, by, bw, bh);
}

static void dialogFire(Dialog *d, AppState *app)
{
    if (d->focused == 0) {
        switch (d->okConfirm) {
            case DLG_CONFIRM_QUIT:     app->pendingAction = PENDING_QUIT;     break;
            case DLG_CONFIRM_NEW_GAME: app->pendingAction = PENDING_NEW_GAME; break;
        }
    }
    /* Whether OK or Cancel, the dialog is dismissed. */
    screenStackPop(&app->screens);
}

static void dialogKey(Dialog *d, SDLKey sym, AppState *app)
{
    if (sym == SDLK_LEFT)  { d->focused = 0; return; }
    if (sym == SDLK_RIGHT) { d->focused = 1; return; }
    if (sym == SDLK_TAB)   { d->focused = !d->focused; return; }
    if (sym == SDLK_ESCAPE) {
        /* Esc always cancels the dialog. */
        d->focused = 1;
        dialogFire(d, app);
        return;
    }
    if (sym == SDLK_RETURN || sym == SDLK_SPACE) {
        dialogFire(d, app);
        return;
    }
}

static void dialogMouse(Dialog *d, float vx, float vy, int clicked, AppState *app)
{
    UiState *ui = app->ui;
    UiRect ok = dialogBtnRect(ui, 0);
    UiRect cn = dialogBtnRect(ui, 1);
    if (uiRectContains(ok, vx, vy)) {
        d->focused = 0;
        if (clicked) dialogFire(d, app);
        return;
    }
    if (uiRectContains(cn, vx, vy)) {
        d->focused = 1;
        if (clicked) dialogFire(d, app);
        return;
    }
}

static void dialogRender(Dialog *d, AppState *app)
{
    UiState *ui = app->ui;
    float halfW = uiGetWidth(ui)  * 0.5f;
    float halfH = uiGetHeight(ui) * 0.5f;

    /* 50% black dim over whatever was drawn below. */
    uiQuad(uiRectMake(-halfW, -halfH, uiGetWidth(ui), uiGetHeight(ui)),
           uiRgba(0, 0, 0, 0.5f));

    UiRect r = dialogRect(ui);

    /* dialog_bg: tile across the rect at native pixel scale. Assuming a
       64x64 source, repeat every 64 virtual pixels. */
    GLuint bgTex = appGetMenuTex(app, "dialog_bg", GL_REPEAT);
    if (bgTex) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, bgTex);
        glColor4f(1, 1, 1, 1);
        float tileVirtual = 64.0f;
        float u1 = r.w / tileVirtual;
        float v1 = r.h / tileVirtual;
        glBegin(GL_QUADS);
            glTexCoord2f(0,  0);  glVertex2f(r.x,       r.y);
            glTexCoord2f(u1, 0);  glVertex2f(r.x + r.w, r.y);
            glTexCoord2f(u1, v1); glVertex2f(r.x + r.w, r.y + r.h);
            glTexCoord2f(0,  v1); glVertex2f(r.x,       r.y + r.h);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    } else {
        uiQuad(r, uiRgba(0.14f, 0.14f, 0.22f, 1.0f));
    }

    /* Border (1-quad outline built from four thin quads). */
    UiColor border = uiRgba(0.9f, 0.9f, 1.0f, 0.5f);
    float b = 2.0f;
    uiQuad(uiRectMake(r.x,           r.y,           r.w, b),   border);
    uiQuad(uiRectMake(r.x,           r.y + r.h - b, r.w, b),   border);
    uiQuad(uiRectMake(r.x,           r.y,           b,   r.h), border);
    uiQuad(uiRectMake(r.x + r.w - b, r.y,           b,   r.h), border);

    UiColor white = uiRgba(1, 1, 1, 1);
    /* Title (dialog_title font) anchored at top-center of dialog interior. */
    uiText(ui, r.x + r.w * 0.5f, r.y + MENU_DLG_PAD,
           white, d->title, 4.5f,
           UI_ALIGN_TOP | UI_ALIGN_CENTER, "dialog_title");

    /* Message anchored mid-height of the dialog, smaller text. */
    uiText(ui, r.x + r.w * 0.5f, r.y + r.h * 0.5f,
           white, d->message, 2.5f,
           UI_ALIGN_MIDDLE | UI_ALIGN_CENTER, "button_font");

    /* Buttons. */
    MenuButton bOk, bCn;
    menuButtonSet(&bOk, "OK",     0, 0, 0, 0, 0, 1);
    menuButtonSet(&bCn, "CANCEL", 0, 0, 0, 0, 0, 1);
    bOk.rect = dialogBtnRect(ui, 0);
    bCn.rect = dialogBtnRect(ui, 1);
    menuDrawButton(ui, &bOk, d->focused == 0);
    menuDrawButton(ui, &bCn, d->focused == 1);
}

/* ---- One-shot loading splash ----
 *
 * Called from main.cpp right before a (synchronous) gameInit run, so the
 * user sees something other than a frozen window while the level loads.
 * Does its own clear + uiBegin/uiEnd + SwapBuffers since it's fired
 * outside the regular frame loop's dispatch. No input handling — control
 * returns to main.cpp immediately after the swap and gameInit blocks
 * the thread until it finishes. */
static void drawLoadingScreen(AppState *app)
{
    UiState *ui = app->ui;
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    uiBegin(ui);
    menuDrawCoverBg(app, "loading_bg", uiRgb(0.04f, 0.03f, 0.08f));

    float halfW = uiGetWidth(ui)  * 0.5f;
    float halfH = uiGetHeight(ui) * 0.5f;
    UiColor fg = uiRgba(1, 1, 1, 1);
    uiText(ui, halfW - MENU_PADDING, halfH - MENU_PADDING,
           fg, "LOADING", 5.0f,
           UI_ALIGN_BOTTOM | UI_ALIGN_RIGHT, "button_font");
    uiEnd(ui);

    SDL_GL_SwapBuffers();
}

/* ---- Screen dispatcher ---- */

static void screenKey(Screen *s, SDLKey sym, AppState *app)
{
    switch (s->type) {
        case SCREEN_MAIN_MENU: mainMenuKey(&s->mm,  sym, app); break;
        case SCREEN_OPTIONS:   optionsKey (&s->opt, sym, app); break;
        case SCREEN_DIALOG:    dialogKey  (&s->dlg, sym, app); break;
    }
}

static void screenMouse(Screen *s, float vx, float vy, int clicked, AppState *app)
{
    switch (s->type) {
        case SCREEN_MAIN_MENU: mainMenuMouse(&s->mm,  vx, vy, clicked, app); break;
        case SCREEN_OPTIONS:   optionsMouse (&s->opt, vx, vy, clicked, app); break;
        case SCREEN_DIALOG:    dialogMouse  (&s->dlg, vx, vy, clicked, app); break;
    }
}

static void screenRender(Screen *s, AppState *app)
{
    switch (s->type) {
        case SCREEN_MAIN_MENU: mainMenuRender(&s->mm,  app); break;
        case SCREEN_OPTIONS:   optionsRender (&s->opt, app); break;
        case SCREEN_DIALOG:    dialogRender  (&s->dlg, app); break;
    }
}

/* ---- App lifecycle ---- */

static void appInit(AppState *app, int screenW, int screenH,
                    UiState *ui, AssetRegistry *ar, ScriptSystem *script,
                    SoundSystem *snd, SoundLibrary *sndLib)
{
    memset(app, 0, sizeof(*app));
    app->mode      = MODE_MENU;
    app->running   = 1;
    app->screenW   = screenW;
    app->screenH   = screenH;
    app->ui        = ui;
    app->assetReg  = ar;
    app->script    = script;
    app->snd       = snd;
    app->sndLib    = sndLib;
    texCacheInit(&app->menuTex);

    Screen mm = makeMainMenu(ui, /*haveGame=*/0);
    screenStackPush(&app->screens, &mm);
}

static void appShutdown(AppState *app)
{
    texCacheFree(&app->menuTex);
    app->screens.count = 0;
}

/* When transitioning back to MENU from GAME, refresh the MainMenu at the
   bottom of the stack so Continue becomes enabled. Called by main.cpp. */
static void appEnterMenu(AppState *app)
{
    app->mode = MODE_MENU;
    if (app->screens.count == 0 ||
        app->screens.items[0].type != SCREEN_MAIN_MENU) {
        screenStackClear(&app->screens);
        Screen mm = makeMainMenu(app->ui, app->game != NULL);
        screenStackPush(&app->screens, &mm);
    } else {
        /* Main menu already at bottom. Re-layout to update CONTINUE enabled
           state (and clear any screens pushed on top so we land on main). */
        while (app->screens.count > 1) screenStackPop(&app->screens);
        mainMenuLayout(&app->screens.items[0].mm, app->ui, app->game != NULL);
    }
}

/* ---- Frame tick (menu mode) ----
 *
 * Poll SDL events, route to the top screen, then draw screens bottom-up
 * so stacked overlays (Options, Dialog) sit on top of MainMenu. Caller is
 * responsible for checking app->pendingAction afterwards and for the
 * GL_COLOR_BUFFER clear + buffer swap. */
static void menuTick(AppState *app, float dt)
{
    (void)dt;

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            app->pendingAction = PENDING_QUIT;
            continue;
        }
        Screen *top = screenStackTop(&app->screens);
        if (!top) continue;

        if (ev.type == SDL_KEYDOWN) {
            screenKey(top, ev.key.keysym.sym, app);
        }
        if (ev.type == SDL_MOUSEMOTION) {
            float vx, vy;
            uiMouseToVirtual(app->ui, ev.motion.x, ev.motion.y, &vx, &vy);
            screenMouse(top, vx, vy, 0, app);
        }
        if (ev.type == SDL_MOUSEBUTTONDOWN &&
            ev.button.button == SDL_BUTTON_LEFT) {
            float vx, vy;
            uiMouseToVirtual(app->ui, ev.button.x, ev.button.y, &vx, &vy);
            screenMouse(top, vx, vy, 1, app);
        }
    }

    /* Render: clear + draw all screens bottom-up. */
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    uiBegin(app->ui);
    for (int i = 0; i < app->screens.count; i++) {
        screenRender(&app->screens.items[i], app);
    }
    uiEnd(app->ui);
}

#endif /* MENU_H */
