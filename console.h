#ifndef CONSOLE_H
#define CONSOLE_H

/*
 * Drop-down developer console (Quake-style). Toggles with backtick while
 * in MODE_GAME. Rolls down to the top half of the screen, shows a ring
 * buffer of log lines with a prompt at the bottom, and runs whatever the
 * user types as a Lua chunk.
 *
 * Log routing: conLogf is a drop-in printf replacement — it writes to
 * stdout (so the terminal / Win98 console window still see everything
 * the engine emits) and, when a Console has been bound via conBind,
 * also pushes the formatted line into the scrollback. conLogf is used
 * throughout the engine in place of printf/fprintf so that once the
 * console is opened, all log output is visible on-screen.
 *
 * Input while open: keyboard is captured by the console (SDL_KEYDOWN is
 * routed to conKey/conText and the per-frame WASD poll is skipped so the
 * player doesn't walk around). Mouse (look + fire) is intentionally not
 * captured — simulation keeps running, just without keyboard input.
 *
 * Must be included after SDL.h, ui.h, texture.h, and asset_registry.h.
 */

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>

#define CON_LINES_MAX  64
#define CON_LINE_LEN   160
#define CON_CMD_LEN    256

struct Console {
    char  lines[CON_LINES_MAX][CON_LINE_LEN];
    int   count;          /* filled entries, capped at CON_LINES_MAX */
    int   head;           /* next write index (ring) */
    char  cmd[CON_CMD_LEN];
    int   cmdLen;
    int   open;           /* target state: 1 = open, 0 = closed */
    float anim;           /* 0..1 — current position, tweened toward `open` */
    float cursorBlink;    /* accumulator for prompt cursor */
};

/* Single-TU global binding. The engine compiles as one TU (main.cpp
   includes every header), so a static here is effectively per-program.
   Keeps conLogf a plain drop-in for printf without threading a Console*
   through every existing log site. */
static Console *g_console = NULL;

/* ---- Lifecycle + logging ---- */

static void conInit(Console *c) { memset(c, 0, sizeof(*c)); }

static void conBind(Console *c) { g_console = c; }

static void conPushRaw(Console *c, const char *s, int len)
{
    if (len < 0) len = 0;
    if (len >= CON_LINE_LEN) len = CON_LINE_LEN - 1;
    char *dst = c->lines[c->head];
    if (len > 0) memcpy(dst, s, len);
    dst[len] = '\0';
    c->head = (c->head + 1) % CON_LINES_MAX;
    if (c->count < CON_LINES_MAX) c->count++;
}

/* Split on '\n' so a single conLogf call with embedded newlines pushes
   multiple scrollback entries (common for multi-line diagnostic output). */
static void conPushLine(Console *c, const char *s)
{
    const char *p = s;
    for (;;) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        if (len > 0 && p[len - 1] == '\r') len--;   /* trim stray CR */
        conPushRaw(c, p, len);
        if (!nl) break;
        p = nl + 1;
        if (!*p) break;  /* trailing newline: don't push an empty final line */
    }
}

static void conLogf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    fputs(buf, stdout);
    if (g_console) conPushLine(g_console, buf);
}

/* ---- State transitions ---- */

static void conToggle(Console *c) { c->open = !c->open; }

static void conUpdate(Console *c, float dt)
{
    /* Roll in/out over ~0.17s (1/6). */
    const float ANIM_SPEED = 6.0f;
    float target = c->open ? 1.0f : 0.0f;
    float delta  = ANIM_SPEED * dt;
    if (c->anim < target) { c->anim += delta; if (c->anim > target) c->anim = target; }
    else if (c->anim > target) { c->anim -= delta; if (c->anim < target) c->anim = target; }
    c->cursorBlink += dt;
}

/* True while the console should capture keyboard input — either open, or
   still rolling in. Once fully rolled up (anim == 0, open == 0), the
   console releases keyboard back to gameplay. */
static int conCapturesInput(const Console *c)
{
    return c->open || c->anim > 0.0f;
}

/* Snap the panel closed and drop the in-progress command, but keep the
   scrollback around. Used when transitioning out to the menu so opening
   the console later shows the same log history. */
static void conClose(Console *c)
{
    c->open = 0;
    c->anim = 0.0f;
    c->cmdLen = 0;
    c->cmd[0] = '\0';
}

/* ---- Text input ---- */

/* Append a single printable Unicode codepoint to the command line. We only
   accept 0x20..0x7E (printable ASCII) — enough for Lua identifiers,
   punctuation, numbers, string literals. Backtick (0x60) is filtered here
   because the toggle handler has already consumed it; the unicode event
   still fires and we don't want ` to leak into cmd. */
static void conText(Console *c, unsigned int uni)
{
    if (uni < 0x20 || uni > 0x7E) return;
    if (uni == '`') return;
    if (c->cmdLen >= CON_CMD_LEN - 1) return;
    c->cmd[c->cmdLen++] = (char)uni;
    c->cmd[c->cmdLen] = '\0';
    c->cursorBlink = 0.0f;  /* Show cursor on each keystroke. */
}

/* conKey status codes — caller uses these to decide what to do next.
 * CON_KEY_NONE     : console didn't consume the key; caller should forward
 *                    it to game handlers or conText for printables.
 * CON_KEY_HANDLED  : console consumed it (backspace, ignored key, empty
 *                    Enter); caller should NOT forward.
 * CON_KEY_EXEC     : user pressed Enter with a non-empty command buffer;
 *                    caller should run conExecute (defined in script.h)
 *                    and then clear the command.
 */
#define CON_KEY_NONE     0
#define CON_KEY_HANDLED  1
#define CON_KEY_EXEC     2

static int conKey(Console *c, int sym)
{
    switch (sym) {
        case 8:    /* SDLK_BACKSPACE */
            if (c->cmdLen > 0) {
                c->cmdLen--;
                c->cmd[c->cmdLen] = '\0';
            }
            c->cursorBlink = 0.0f;
            return CON_KEY_HANDLED;
        case 13:   /* SDLK_RETURN */
        case 271:  /* SDLK_KP_ENTER */
            c->cursorBlink = 0.0f;
            return (c->cmdLen > 0) ? CON_KEY_EXEC : CON_KEY_HANDLED;
        default:
            return CON_KEY_NONE;
    }
}

/* ---- Rendering ----
 *
 * Panel fills the top half of the virtual canvas × anim. Background is
 * the tiled `dialog_bg` texture; a 2-virtual-px accent line marks the
 * bottom edge of the console. Scrollback lines are drawn bottom-up from
 * just above the prompt, soft-clipped against the animated top edge.
 * Cursor glyph (`_`) blinks at 2Hz. */

#define CON_PAD_X       9.0f
/* uiText scales — new convention: scale * font.lineHeight = on-screen
   line height in vpx. Default font is orbitron_small (lineHeight = 28).
   Pre-SOOB-Core targets were 16 vpx (scrollback) and 20 vpx (prompt). */
#define CON_LINE_SCALE   (16.0f / 28.0f)   /* scrollback lines  */
#define CON_PROMPT_SCALE (20.0f / 28.0f)   /* prompt + input    */
#define CON_DIVIDER_PX  2.0f

static void conRender(Console *c, UiState *ui, GLuint bgTex)
{
    if (c->anim <= 0.0f) return;

    float vw = uiGetWidth(ui);
    float vh = uiGetHeight(ui);
    float halfW = vw * 0.5f;
    float halfH = vh * 0.5f;
    float fullH = vh * 0.5f;
    float panelH = fullH * c->anim;

    UiRect panel = uiRectMake(-halfW, -halfH, vw, panelH);

    /* Tiled dialog_bg background (caller resolves the GL texture via
       whichever asset/cache path it uses — this header keeps zero
       dependency on texture.h / asset_registry.h). Falls back to a flat
       dark fill if the caller passed 0. Translucent via glColor alpha. */
    if (bgTex) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, bgTex);
        glColor4f(1, 1, 1, 0.88f);
        const float tileV = 32.0f;
        float u1 = panel.w / tileV;
        float v1 = panel.h / tileV;
        glBegin(GL_QUADS);
            glTexCoord2f(0,  0);  glVertex2f(panel.x,           panel.y);
            glTexCoord2f(u1, 0);  glVertex2f(panel.x + panel.w, panel.y);
            glTexCoord2f(u1, v1); glVertex2f(panel.x + panel.w, panel.y + panel.h);
            glTexCoord2f(0,  v1); glVertex2f(panel.x,           panel.y + panel.h);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    } else {
        uiQuad(panel, uiRgba(0.05f, 0.05f, 0.12f, 0.88f));
    }

    /* Bottom divider. */
    UiColor divider = uiRgba(0.55f, 0.30f, 0.90f, 1.0f);
    uiQuad(uiRectMake(panel.x, panel.y + panel.h - CON_DIVIDER_PX,
                      panel.w, CON_DIVIDER_PX),
           divider);

    /* Line spacing follows the same convention as uiText: rendered text
       height = scale * font.lineHeight. Look up the default font (NULL ->
       "default") so spacing matches whatever uiText actually drew. Falls
       back to the 8x8 bitmap's native height when no BMFont is loaded. */
    UiFont *refFont = uiFontFind(&ui->fonts, NULL);
    float refLh = refFont ? (float)refFont->lineHeight : 8.0f;

    /* Prompt strip lives one line height above the divider. */
    float promptLineH = CON_PROMPT_SCALE * refLh;
    float logLineH    = CON_LINE_SCALE   * refLh;
    float promptY     = panel.y + panel.h - CON_DIVIDER_PX - promptLineH;
    float logTopBound = panel.y + 3.0f;  /* don't draw lines above this */

    UiColor fgLog    = uiRgba(0.90f, 0.90f, 0.95f, 1.0f);
    UiColor fgPrompt = uiRgba(0.55f, 1.00f, 0.85f, 1.0f); /* cyan/mint */
    UiColor fgInput  = uiRgba(1.00f, 1.00f, 1.00f, 1.0f);

    /* Scrollback: walk backward from newest, drawing upward until we hit
       logTopBound (soft-clip). Oldest-first visible order looks natural. */
    float y = promptY - logLineH - 2.0f;
    for (int i = 0; i < c->count; i++) {
        int idx = (c->head - 1 - i + CON_LINES_MAX) % CON_LINES_MAX;
        if (y < logTopBound) break;
        uiText(ui, panel.x + CON_PAD_X, y, fgLog,
               c->lines[idx], CON_LINE_SCALE,
               UI_ALIGN_TOP | UI_ALIGN_LEFT, NULL);
        y -= logLineH;
    }

    /* Prompt: "> " accent, then the user's current command in white,
       then a blinking cursor. Three uiText calls so the color can change
       mid-line; widths come from uiTextWidth so the cursor sits flush
       against the last typed glyph (Orbitron is proportional, so
       per-char averages would drift as the line grows). */
    float px = panel.x + CON_PAD_X;
    uiText(ui, px, promptY, fgPrompt, "> ",
           CON_PROMPT_SCALE, UI_ALIGN_TOP | UI_ALIGN_LEFT, NULL);
    px += uiTextWidth(ui, "> ", CON_PROMPT_SCALE, NULL);
    if (c->cmdLen > 0) {
        uiText(ui, px, promptY, fgInput, c->cmd,
               CON_PROMPT_SCALE, UI_ALIGN_TOP | UI_ALIGN_LEFT, NULL);
        px += uiTextWidth(ui, c->cmd, CON_PROMPT_SCALE, NULL);
    }
    int blinkOn = ((int)(c->cursorBlink * 2.0f) & 1) == 0;
    if (blinkOn) {
        uiText(ui, px, promptY, fgPrompt, "_",
               CON_PROMPT_SCALE, UI_ALIGN_TOP | UI_ALIGN_LEFT, NULL);
    }
}

#endif /* CONSOLE_H */
