#ifndef UI_H
#define UI_H

/*
 * Header-only UI/HUD primitives: bitmap text, filled quads, progress bars,
 * textured icons. Draws in ortho-projected pixel coordinates (top-left is
 * (0,0); X grows right, Y grows down) to match typical 2D UI conventions.
 *
 * Use between uiBegin(&ui) and uiEnd(&ui). The helpers manage GL state
 * (depth, lighting, blending) so UI calls don't leak into world rendering.
 *
 * Font: 8x8 monochrome ASCII (U+0000..U+007F) from Daniel Hepper's
 *   https://github.com/dhepper/font8x8 (ultimately derived from IBM's
 *   public-domain VGA font). Public Domain. Atlas is built in-memory at
 *   uiInit(); no disk file needed.
 */

#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* 128 glyphs × 8 rows × 1 byte. LSB = leftmost pixel. */
static const unsigned char ui_font8x8[128][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, /* ! */
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, /* # */
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, /* $ */
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, /* % */
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, /* & */
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, /* ( */
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, /* ) */
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, /* * */
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, /* , */
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, /* . */
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, /* / */
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, /* 0 */
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, /* 1 */
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, /* 2 */
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, /* 3 */
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, /* 4 */
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, /* 5 */
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, /* 6 */
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, /* 7 */
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, /* 8 */
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, /* 9 */
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, /* : */
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, /* ; */
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, /* < */
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, /* = */
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, /* > */
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, /* ? */
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, /* @ */
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, /* A */
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, /* B */
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, /* C */
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, /* D */
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, /* E */
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, /* F */
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, /* G */
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, /* H */
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* I */
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, /* J */
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, /* K */
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, /* L */
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, /* M */
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, /* N */
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, /* O */
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, /* P */
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, /* Q */
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, /* R */
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, /* S */
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* T */
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, /* U */
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* V */
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, /* W */
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, /* X */
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, /* Y */
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, /* Z */
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, /* [ */
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, /* \ */
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, /* ] */
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, /* _ */
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, /* a */
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, /* b */
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, /* c */
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, /* d */
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, /* e */
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, /* f */
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, /* g */
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, /* h */
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, /* i */
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, /* j */
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, /* k */
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* l */
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, /* m */
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, /* n */
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, /* o */
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, /* p */
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, /* q */
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, /* r */
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, /* s */
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, /* t */
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, /* u */
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* v */
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, /* w */
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, /* x */
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, /* y */
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, /* z */
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, /* { */
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, /* | */
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, /* } */
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, /* ~ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};

/* Atlas layout: 16 glyphs across × 8 rows. Each cell is 10x10 pixels with
   a 1-pixel transparent border and the 8x8 glyph in the middle. The border
   lets GL_LINEAR sample cleanly at glyph edges — without it, neighboring
   glyphs bleed into each other when text is drawn at fractional positions.
   Atlas dimensions are padded to the next power of two (256x128) because
   Win98 / OpenGL 1.1 drivers don't accept NPOT textures — a non-POT upload
   there silently produces a white texture, which makes every glyph render
   as a solid filled rectangle. */
#define UI_ATLAS_W      256  /* POT; only the first 160 px hold glyphs */
#define UI_ATLAS_H      128  /* POT; only the first 80 px hold glyphs  */
#define UI_GLYPH_PX     8
#define UI_CELL_PX      10   /* glyph + 1px border each side */

/* Virtual canvas: UI coordinates are in "virtual pixels" relative to a
   fixed 1080-unit height. Virtual width scales with aspect ratio so
   wider screens show more horizontal space, not stretched text. The
   origin (0, 0) is at screen CENTER; Y grows down (screen convention).
     top edge    y = -uiGetHeight() / 2
     bottom edge y = +uiGetHeight() / 2
     left edge   x = -uiGetWidth()  / 2
     right edge  x = +uiGetWidth()  / 2
*/
#define UI_VIRTUAL_H 1080.0f

struct UiRect  { float x, y, w, h; };
struct UiColor { float r, g, b, a; };

/* ---- BMFont (AngelCode) font library ----
 *
 * Bitmap fonts generated by AngelCode BMFont / fontbm / Hiero. Atlas is a
 * 32-bit RGBA TGA — the alpha channel carries the antialiased glyph mask,
 * RGB is white-ish and gets modulated by the caller's text color. Only the
 * text-form .fnt (not binary) is supported; only ASCII (0..127) chars map
 * into the glyph table; kerning pairs are ignored in v1. */

#define UI_FONT_MAX        8
#define UI_FONT_GLYPH_MAX  128
#define UI_FONT_NAME_MAX   32
#define UI_FONT_PATH_MAX   192

struct UiGlyph {
    short x, y;          /* top-left in atlas pixels */
    short w, h;          /* glyph size in atlas pixels */
    short xoffset;       /* pen → glyph top-left, font pixels */
    short yoffset;
    short xadvance;      /* pen advance, font pixels */
    short valid;         /* 0 = char not present in this font */
};

struct UiFont {
    char    name[UI_FONT_NAME_MAX];
    char    sourcePath[UI_FONT_PATH_MAX]; /* .fnt path — used for dedupe */
    GLuint  tex;
    int     atlasW, atlasH;
    int     lineHeight;
    int     base;
    UiGlyph glyphs[UI_FONT_GLYPH_MAX];
    int     ownsTex;   /* 1 → this entry deletes `tex` at shutdown; 0 → alias */
};

struct UiFontLib {
    UiFont fonts[UI_FONT_MAX];
    int    count;
};

/* Alignment flags. Vertical bits 0..1, horizontal bits 2..4. Combine with `|`.
   Default for uiText is TOP|LEFT (anchor at top-left, matches old behavior). */
#define UI_ALIGN_TOP     0
#define UI_ALIGN_MIDDLE  1
#define UI_ALIGN_BOTTOM  2
#define UI_ALIGN_LEFT    4
#define UI_ALIGN_CENTER  8
#define UI_ALIGN_RIGHT   16

static UiRect uiRectMake(float x, float y, float w, float h) {
    UiRect r; r.x = x; r.y = y; r.w = w; r.h = h; return r;
}
static UiColor uiRgb(float r, float g, float b) {
    UiColor c; c.r = r; c.g = g; c.b = b; c.a = 1.0f; return c;
}
static UiColor uiRgba(float r, float g, float b, float a) {
    UiColor c; c.r = r; c.g = g; c.b = b; c.a = a; return c;
}

struct UiState {
    GLuint fontTex;    /* 8x8 fallback atlas (used if no BMFont loaded) */
    int screenW;       /* real pixel size (for aspect only) */
    int screenH;
    float virtualW;    /* derived: UI_VIRTUAL_H * aspect */
    float virtualH;    /* UI_VIRTUAL_H (1080) */

    /* Transient centered message — shown near the top of the screen, fades
       out over the last UI_MSG_FADE seconds of its lifetime. msgTimeLeft > 0
       means a message is active. Driven by uiShowMessage + uiUpdateMessage;
       rendered by uiDrawMessage (call inside uiBegin/uiEnd). */
    char  msgText[128];
    float msgTimeLeft;
    float msgTotal;

    /* BMFont library. Populated by uiFontLoad from assets.lua at startup.
       uiText looks fonts up by name here; if no matching font (or the lib
       is empty), it falls back to the 8x8 fontTex atlas above. */
    UiFontLib fonts;
};

static float uiGetWidth(UiState *ui)  { return ui->virtualW; }
static float uiGetHeight(UiState *ui) { return ui->virtualH; }

/* Convert a pixel-space mouse position (SDL reports top-left origin,
   Y-down) to the virtual canvas used by uiBegin/uiText. Virtual origin
   is the screen center with Y-down, same handedness as the ortho set in
   uiBegin. Safe to call even when GL ortho isn't currently active —
   it's a pure coordinate transform on UiState's cached dimensions. */
static void uiMouseToVirtual(UiState *ui, int px, int py, float *vx, float *vy)
{
    if (ui->screenW <= 0 || ui->screenH <= 0) { *vx = 0; *vy = 0; return; }
    float u = (float)px / (float)ui->screenW;
    float v = (float)py / (float)ui->screenH;
    *vx = (u - 0.5f) * ui->virtualW;
    *vy = (v - 0.5f) * ui->virtualH;
}

static int uiRectContains(UiRect r, float x, float y)
{
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

static void uiInit(UiState *ui, int screenW, int screenH)
{
    memset(ui, 0, sizeof(*ui));
    ui->screenW = screenW;
    ui->screenH = screenH;
    ui->virtualH = UI_VIRTUAL_H;
    ui->virtualW = UI_VIRTUAL_H * (float)screenW / (float)screenH;

    unsigned char *px = (unsigned char *)malloc(UI_ATLAS_W * UI_ATLAS_H * 4);
    memset(px, 0, UI_ATLAS_W * UI_ATLAS_H * 4);
    for (int g = 0; g < 128; g++) {
        int cellX = (g % 16) * UI_CELL_PX;
        int cellY = (g / 16) * UI_CELL_PX;
        for (int y = 0; y < UI_GLYPH_PX; y++) {
            unsigned char row = ui_font8x8[g][y];
            for (int x = 0; x < UI_GLYPH_PX; x++) {
                int on = (row >> x) & 1;
                /* +1 offset puts the glyph inside the 1-px border */
                int ax = cellX + 1 + x;
                int ay = cellY + 1 + y;
                int off = (ay * UI_ATLAS_W + ax) * 4;
                px[off + 0] = 255;
                px[off + 1] = 255;
                px[off + 2] = 255;
                px[off + 3] = on ? 255 : 0;
            }
        }
    }
    glGenTextures(1, &ui->fontTex);
    glBindTexture(GL_TEXTURE_2D, ui->fontTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, UI_ATLAS_W, UI_ATLAS_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, px);
    free(px);
    printf("ui: font atlas %dx%d built\n", UI_ATLAS_W, UI_ATLAS_H);

    ui->fonts.count = 0;
}

static void uiShutdown(UiState *ui)
{
    for (int i = 0; i < ui->fonts.count; i++) {
        UiFont *f = &ui->fonts.fonts[i];
        if (f->ownsTex && f->tex) glDeleteTextures(1, &f->tex);
    }
    ui->fonts.count = 0;
    glDeleteTextures(1, &ui->fontTex);
    ui->fontTex = 0;
}

/* ---- BMFont loading ----
 *
 * uiFontLoad parses a .fnt text file (AngelCode/fontbm output), uploads the
 * referenced atlas TGA as an OpenGL texture, and adds the result to the
 * library keyed by `name`. The texture path is resolved relative to the
 * .fnt's directory (the .fnt stores only the filename). */

/* Uncompressed 32-bit RGBA TGA loader (BMFont atlas format). Returns 0 on
   failure. Kept local to ui.h so the font system has no dependency on
   texture.h — font atlases need the alpha channel that texture.h strips. */
static GLuint uiLoadTGA32(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ui: cannot open font atlas %s\n", path);
        return 0;
    }
    unsigned char hdr[18];
    if (fread(hdr, 1, 18, f) != 18) { fclose(f); return 0; }
    if (hdr[2] != 2 || hdr[16] != 32) {
        fprintf(stderr, "ui: %s must be uncompressed 32-bit TGA\n", path);
        fclose(f);
        return 0;
    }
    int w = hdr[12] | (hdr[13] << 8);
    int h = hdr[14] | (hdr[15] << 8);
    if (hdr[0]) fseek(f, hdr[0], SEEK_CUR);

    int dataSz = w * h * 4;
    unsigned char *raw = (unsigned char *)malloc(dataSz);
    fread(raw, 1, dataSz, f);
    fclose(f);

    /* TGA stores BGRA; flip to RGBA. Bottom-up unless the origin bit is set. */
    int topDown = (hdr[17] & 0x20) != 0;
    unsigned char *rgba = (unsigned char *)malloc(dataSz);
    for (int y = 0; y < h; y++) {
        int sy = topDown ? y : (h - 1 - y);
        unsigned char *src = raw + sy * w * 4;
        unsigned char *dst = rgba + y * w * 4;
        for (int x = 0; x < w; x++) {
            dst[x*4+0] = src[x*4+2];
            dst[x*4+1] = src[x*4+1];
            dst[x*4+2] = src[x*4+0];
            dst[x*4+3] = src[x*4+3];
        }
    }
    free(raw);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
    return tex;
}

/* Match "key=<int>" at a word boundary in `line` and parse the int. */
static int uiFntInt(const char *line, const char *key, int *out)
{
    int kl = (int)strlen(key);
    for (const char *p = line; *p; p++) {
        if ((p == line || p[-1] == ' ' || p[-1] == '\t') &&
            strncmp(p, key, kl) == 0 && p[kl] == '=') {
            *out = atoi(p + kl + 1);
            return 1;
        }
    }
    return 0;
}

/* Match key="..." in `line` and copy the quoted value into `out`. */
static int uiFntQuoted(const char *line, const char *key, char *out, int outSz)
{
    int kl = (int)strlen(key);
    for (const char *p = line; *p; p++) {
        if ((p == line || p[-1] == ' ' || p[-1] == '\t') &&
            strncmp(p, key, kl) == 0 && p[kl] == '=' && p[kl + 1] == '"') {
            const char *s = p + kl + 2;
            const char *e = strchr(s, '"');
            if (!e) return 0;
            int n = (int)(e - s);
            if (n >= outSz) n = outSz - 1;
            memcpy(out, s, n);
            out[n] = '\0';
            return 1;
        }
    }
    return 0;
}

static int uiFontLoad(UiFontLib *lib, const char *name, const char *fntPath)
{
    if (lib->count >= UI_FONT_MAX) {
        fprintf(stderr, "ui: font library full, cannot load '%s'\n", name);
        return 0;
    }

    /* Dedupe: if the same .fnt was already loaded under a different name,
       create an alias entry that shares the GL texture and glyph table. */
    for (int i = 0; i < lib->count; i++) {
        if (strcmp(lib->fonts[i].sourcePath, fntPath) == 0) {
            UiFont *src = &lib->fonts[i];
            UiFont *dst = &lib->fonts[lib->count];
            memcpy(dst, src, sizeof(*dst));
            strncpy(dst->name, name, UI_FONT_NAME_MAX - 1);
            dst->name[UI_FONT_NAME_MAX - 1] = '\0';
            dst->ownsTex = 0;
            lib->count++;
            printf("ui: font '%s' aliased to '%s' (%s)\n",
                   name, src->name, fntPath);
            return 1;
        }
    }

    FILE *f = fopen(fntPath, "rb");
    if (!f) {
        fprintf(stderr, "ui: cannot open .fnt %s\n", fntPath);
        return 0;
    }

    UiFont *font = &lib->fonts[lib->count];
    memset(font, 0, sizeof(*font));
    strncpy(font->name, name, UI_FONT_NAME_MAX - 1);
    strncpy(font->sourcePath, fntPath, UI_FONT_PATH_MAX - 1);
    font->ownsTex = 1;

    char atlasFile[128];
    atlasFile[0] = '\0';
    char line[512];

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "common ", 7) == 0) {
            uiFntInt(line, "lineHeight", &font->lineHeight);
            uiFntInt(line, "base",       &font->base);
            uiFntInt(line, "scaleW",     &font->atlasW);
            uiFntInt(line, "scaleH",     &font->atlasH);
        } else if (strncmp(line, "page ", 5) == 0) {
            uiFntQuoted(line, "file", atlasFile, sizeof(atlasFile));
        } else if (strncmp(line, "char ", 5) == 0) {
            int id = 0, x = 0, y = 0, w = 0, h = 0;
            int xo = 0, yo = 0, xa = 0;
            uiFntInt(line, "id",       &id);
            uiFntInt(line, "x",        &x);
            uiFntInt(line, "y",        &y);
            uiFntInt(line, "width",    &w);
            uiFntInt(line, "height",   &h);
            uiFntInt(line, "xoffset",  &xo);
            uiFntInt(line, "yoffset",  &yo);
            uiFntInt(line, "xadvance", &xa);
            if (id >= 0 && id < UI_FONT_GLYPH_MAX) {
                UiGlyph *g = &font->glyphs[id];
                g->x = (short)x; g->y = (short)y;
                g->w = (short)w; g->h = (short)h;
                g->xoffset = (short)xo; g->yoffset = (short)yo;
                g->xadvance = (short)xa;
                g->valid = 1;
            }
        }
        /* info / chars / kerning — ignored in v1. */
    }
    fclose(f);

    if (!atlasFile[0]) {
        fprintf(stderr, "ui: %s missing 'page file=\"...\"' line\n", fntPath);
        return 0;
    }

    /* Resolve atlas path relative to the .fnt's own directory. */
    char atlasPath[UI_FONT_PATH_MAX];
    int lastSlash = -1;
    int pathLen = (int)strlen(fntPath);
    for (int i = 0; i < pathLen; i++) {
        if (fntPath[i] == '/' || fntPath[i] == '\\') lastSlash = i;
    }
    if (lastSlash >= 0) {
        int pre = lastSlash + 1;
        if (pre >= UI_FONT_PATH_MAX) pre = UI_FONT_PATH_MAX - 1;
        memcpy(atlasPath, fntPath, pre);
        atlasPath[pre] = '\0';
        strncat(atlasPath, atlasFile, UI_FONT_PATH_MAX - pre - 1);
    } else {
        strncpy(atlasPath, atlasFile, UI_FONT_PATH_MAX - 1);
        atlasPath[UI_FONT_PATH_MAX - 1] = '\0';
    }

    font->tex = uiLoadTGA32(atlasPath);
    if (!font->tex) return 0;

    lib->count++;
    printf("ui: font '%s' loaded from %s (line %d, base %d, atlas %dx%d)\n",
           name, fntPath, font->lineHeight, font->base,
           font->atlasW, font->atlasH);
    return 1;
}

/* Look a font up by name. NULL or an unknown name resolves to "default"
   when loaded; if no match at all, returns NULL (caller falls back). */
static UiFont *uiFontFind(UiFontLib *lib, const char *name)
{
    if (!name) name = "default";
    for (int i = 0; i < lib->count; i++) {
        if (strcmp(lib->fonts[i].name, name) == 0) return &lib->fonts[i];
    }
    if (strcmp(name, "default") != 0) {
        for (int i = 0; i < lib->count; i++) {
            if (strcmp(lib->fonts[i].name, "default") == 0) return &lib->fonts[i];
        }
    }
    return NULL;
}

/* Enter 2D/ortho mode. Pixel coords, (0,0) top-left.
   All UI draws must be between uiBegin/uiEnd.

   Note on culling: our ortho flips Y (top=0, bottom=screenH), so quads
   authored with natural top-left-to-bottom-left winding come out back-
   facing in NDC. With GL_CULL_FACE enabled at init, they'd be culled.
   Disable culling while the UI draws. */
static void uiBegin(UiState *ui)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    /* Virtual canvas: center origin, Y-down. */
    float halfW = ui->virtualW * 0.5f;
    float halfH = ui->virtualH * 0.5f;
    glOrtho(-halfW, +halfW, +halfH, -halfH, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

static void uiEnd(UiState * /*ui*/)
{
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

/* Filled, flat-colored rectangle. */
static void uiQuad(UiRect r, UiColor c)
{
    glDisable(GL_TEXTURE_2D);
    glColor4f(c.r, c.g, c.b, c.a);
    glBegin(GL_QUADS);
        glVertex2f(r.x,       r.y);
        glVertex2f(r.x + r.w, r.y);
        glVertex2f(r.x + r.w, r.y + r.h);
        glVertex2f(r.x,       r.y + r.h);
    glEnd();
}

/* Textured rectangle (e.g., weapon icon, key pickup). */
static void uiIcon(UiRect r, GLuint tex)
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(r.x,       r.y);
        glTexCoord2f(1, 0); glVertex2f(r.x + r.w, r.y);
        glTexCoord2f(1, 1); glVertex2f(r.x + r.w, r.y + r.h);
        glTexCoord2f(0, 1); glVertex2f(r.x,       r.y + r.h);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

/* Bitmap text.
   (x, y) is the anchor — by default the top-left corner of the first line.
   `align` shifts the anchor: MIDDLE|CENTER anchors the text's midpoint at
   (x, y), RIGHT anchors the right edge, etc.
   `scale` keeps its legacy meaning: the line renders at (scale * 8) virtual
   pixels tall. For BMFont rendering the font's native lineHeight is scaled
   to match, so existing HUD calls stay visually consistent.
   `fontName` looks up a loaded BMFont (NULL → "default"). If no font is
   registered, the built-in 8x8 atlas is used as a fallback. */
static void uiText(UiState *ui, float x, float y, UiColor c, const char *text,
                   float scale = 3.0f,
                   int align = UI_ALIGN_TOP | UI_ALIGN_LEFT,
                   const char *fontName = NULL)
{
    UiFont *font = uiFontFind(&ui->fonts, fontName);

    if (!font) {
        /* Fallback: built-in 8x8 bitmap font. */
        const float gw = 8.0f * scale;
        const float gh = 8.0f * scale;

        int len = 0;
        for (const char *p = text; *p; p++) len++;
        float textW = len * gw;

        if (align & UI_ALIGN_CENTER)      x -= textW * 0.5f;
        else if (align & UI_ALIGN_RIGHT)  x -= textW;
        if (align & UI_ALIGN_MIDDLE)      y -= gh * 0.5f;
        else if (align & UI_ALIGN_BOTTOM) y -= gh;

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, ui->fontTex);
        glColor4f(c.r, c.g, c.b, c.a);
        /* UVs span the inner 8x8 of each 10x10 cell (skipping the 1px border
           that exists to keep GL_LINEAR from bleeding neighbor glyphs). */
        const float cellU  = (float)UI_CELL_PX  / (float)UI_ATLAS_W;
        const float cellV  = (float)UI_CELL_PX  / (float)UI_ATLAS_H;
        const float insetU = 1.0f               / (float)UI_ATLAS_W;
        const float insetV = 1.0f               / (float)UI_ATLAS_H;
        const float glyphU = (float)UI_GLYPH_PX / (float)UI_ATLAS_W;
        const float glyphV = (float)UI_GLYPH_PX / (float)UI_ATLAS_H;
        float px = x;
        glBegin(GL_QUADS);
        for (const char *p = text; *p; p++) {
            unsigned char ch = (unsigned char)*p;
            if (ch >= 128) ch = '?';
            float u0 = (ch % 16) * cellU + insetU;
            float v0 = (ch / 16) * cellV + insetV;
            float u1 = u0 + glyphU;
            float v1 = v0 + glyphV;
            glTexCoord2f(u0, v0); glVertex2f(px,      y);
            glTexCoord2f(u1, v0); glVertex2f(px + gw, y);
            glTexCoord2f(u1, v1); glVertex2f(px + gw, y + gh);
            glTexCoord2f(u0, v1); glVertex2f(px,      y + gh);
            px += gw;
        }
        glEnd();
        glDisable(GL_TEXTURE_2D);
        return;
    }

    /* BMFont render path. */
    float rs = (scale * 8.0f) / (float)font->lineHeight;

    /* Measure for alignment. */
    float textW = 0.0f;
    for (const char *p = text; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch >= UI_FONT_GLYPH_MAX || !font->glyphs[ch].valid) ch = '?';
        textW += font->glyphs[ch].xadvance * rs;
    }
    float textH = font->lineHeight * rs;

    if (align & UI_ALIGN_CENTER)      x -= textW * 0.5f;
    else if (align & UI_ALIGN_RIGHT)  x -= textW;
    if (align & UI_ALIGN_MIDDLE)      y -= textH * 0.5f;
    else if (align & UI_ALIGN_BOTTOM) y -= textH;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, font->tex);
    glColor4f(c.r, c.g, c.b, c.a);

    const float invW = 1.0f / (float)font->atlasW;
    const float invH = 1.0f / (float)font->atlasH;
    float pen = x;
    glBegin(GL_QUADS);
    for (const char *p = text; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch >= UI_FONT_GLYPH_MAX || !font->glyphs[ch].valid) ch = '?';
        UiGlyph *g = &font->glyphs[ch];
        float gx = pen + g->xoffset * rs;
        float gy = y   + g->yoffset * rs;
        float gw = g->w * rs;
        float gh = g->h * rs;
        float u0 = g->x * invW;
        float v0 = g->y * invH;
        float u1 = (g->x + g->w) * invW;
        float v1 = (g->y + g->h) * invH;
        glTexCoord2f(u0, v0); glVertex2f(gx,      gy);
        glTexCoord2f(u1, v0); glVertex2f(gx + gw, gy);
        glTexCoord2f(u1, v1); glVertex2f(gx + gw, gy + gh);
        glTexCoord2f(u0, v1); glVertex2f(gx,      gy + gh);
        pen += g->xadvance * rs;
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

/* ---- Transient message ----
   Shown near the top of the screen, fades over the last UI_MSG_FADE
   seconds. Calling uiShowMessage mid-display overwrites the current
   message (no queueing in v1). */
#define UI_MSG_FADE 0.5f

static void uiShowMessage(UiState *ui, const char *text, float seconds)
{
    if (seconds <= 0.0f) seconds = 3.0f;
    strncpy(ui->msgText, text, sizeof(ui->msgText) - 1);
    ui->msgText[sizeof(ui->msgText) - 1] = '\0';
    ui->msgTimeLeft = seconds;
    ui->msgTotal    = seconds;
}

static void uiUpdateMessage(UiState *ui, float dt)
{
    if (ui->msgTimeLeft > 0.0f) {
        ui->msgTimeLeft -= dt;
        if (ui->msgTimeLeft < 0.0f) ui->msgTimeLeft = 0.0f;
    }
}

static void uiDrawMessage(UiState *ui)
{
    if (ui->msgTimeLeft <= 0.0f) return;
    float alpha = 1.0f;
    if (ui->msgTimeLeft < UI_MSG_FADE) alpha = ui->msgTimeLeft / UI_MSG_FADE;
    UiColor c = uiRgba(1, 1, 1, alpha);
    /* Anchored ~25% from the top; centered horizontally. */
    float y = -uiGetHeight(ui) * 0.25f;
    uiText(ui, 0.0f, y, c, ui->msgText,
           3.5f, UI_ALIGN_TOP | UI_ALIGN_CENTER);
}

/* Horizontal progress bar: dark background, white border, colored fill.
   Border is 3 virtual px — thick enough to stay readable on low-res
   displays once the virtual canvas is scaled down. fillPct clamped [0,1]. */
#define UI_BAR_BORDER 3.0f
static void uiBar(UiRect r, float fillPct, UiColor fill)
{
    if (fillPct < 0.0f) fillPct = 0.0f;
    if (fillPct > 1.0f) fillPct = 1.0f;
    UiColor bg     = uiRgba(0, 0, 0, 0.7f);
    UiColor border = uiRgba(1, 1, 1, 0.6f);
    const float b = UI_BAR_BORDER;
    uiQuad(r, bg);
    uiQuad(uiRectMake(r.x,           r.y,           r.w, b),   border);
    uiQuad(uiRectMake(r.x,           r.y + r.h - b, r.w, b),   border);
    uiQuad(uiRectMake(r.x,           r.y,           b,   r.h), border);
    uiQuad(uiRectMake(r.x + r.w - b, r.y,           b,   r.h), border);
    if (fillPct > 0.0f) {
        uiQuad(uiRectMake(r.x + b, r.y + b,
                          (r.w - 2*b) * fillPct, r.h - 2*b), fill);
    }
}

#endif /* UI_H */
