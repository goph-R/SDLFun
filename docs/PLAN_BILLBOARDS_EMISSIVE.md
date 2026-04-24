# Light-Effect Billboards + Emissive Maps

## Context

Alpha-test (cutout) is already in. Two things remain for "lighting that looks like lighting":

1. **Billboards** — camera-facing sprites for lamp glows, fireballs, sparks, lens-flare cores. These read as point lights even though they're flat textures.
2. **Emissive maps** — a per-material second texture whose color is added on top of `diffuse × lightmap`, so a monitor screen, an LED strip, or a lava crack stays bright in an otherwise dark room.

Both must fit inside the **GeForce 4 MX 440 / 32MB / 2 TMUs** envelope set in CLAUDE.md. That rules out any single-pass combiner stack deeper than two textures; emissive has to be a second pass.

## Order of work

Billboards first — they are completely independent of the level render path and can ship as a self-contained module. Emissive touches `renderLevelSectored`, the MTL parser, the tex cache, and needs a consistent story with the dynamic flashlight lightmap; it's riskier and benefits from having billboards already landed so the two don't fight in one PR.

---

## Part 1: Light-effect billboards

### Goal

A flat texture quad that:
- always faces the camera (spherical billboarding — roll as well as yaw),
- draws with **additive blending** so it reads as emitted light, not an object,
- does not z-write (so two overlapping glows don't occlude each other), but does z-test (so a glow behind a wall is hidden),
- needs no sorting (additive is commutative).

### Data

New `EntityType`:

```c
ENT_BILLBOARD
```

Fields on `Entity` (already has pos/rot/scale):
```c
struct {
    GLuint tex;          /* 32-bit RGBA, loaded with keepAlpha=1 */
    float r, g, b;       /* tint multiplied into fragment color */
    float size;          /* world-space half-extent */
    float flicker;       /* 0 = steady, >0 = noise amplitude */
    float phase;         /* per-instance seed */
} billboard;
```

`.ent` line format (following the existing `type name group x y z rotY key=value` convention; `rotY` is ignored):
```
billboard lamp0 lights 3.5 2.8 -1.2 0 tex=lamp_glow size=0.6 color=1,0.9,0.6 flicker=0.2
```

Asset registry gets a `billboards` table in `assets.lua` so the file can reference `tex=lamp_glow` instead of a full path (same pattern as `textures`).

### Render path

Add `entRenderBillboards(EntityList*, float viewRight[3], float viewUp[3])`, called from `main.cpp` **after** entities, **after** the level, **before** HUD / crosshair. It walks the entity list once, skips inactive and non-billboards, and issues one `GL_QUADS` batch.

Per billboard:
```
corner = pos ± right*size ± up*size
```
where `right` and `up` are the camera basis vectors — extract them once per frame from the already-computed view matrix in `glLookAt` (store them next to `eyeX/Y/Z` in `Game` rather than re-reading `GL_MODELVIEW_MATRIX`).

GL state block around the batch:
```c
glDepthMask(GL_FALSE);
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE);   /* additive, pre-multiplied by alpha */
glDisable(GL_LIGHTING);
glDisable(GL_CULL_FACE);              /* quads are already facing camera, but keep it off to be safe */
/* ... glBegin/glEnd ... */
glDepthMask(GL_TRUE);
glDisable(GL_BLEND);
glEnable(GL_LIGHTING);
glEnable(GL_CULL_FACE);
```

`glColor4f(r*I, g*I, b*I, 1)` where `I = 1 + flicker * (noise(t + phase) - 0.5)` — tint and intensity fold into vertex color; the texture supplies the shape.

### Why this survives MX 440

- One TMU, no combiner tricks.
- Additive blend is standard GL 1.1.
- Quads (not triangles) are native fast-path on all DX7 hardware.
- No sort, no per-frame CPU work per billboard beyond a 4-vertex emit.

### Open questions

1. **Depth-fade near walls**: a glow that clips straight through geometry looks wrong. Simplest fix for later — raycast once at spawn to shrink `size` if close to a surface. Defer.
2. **Scripted spawn**: do we want `billboard.spawn(x,y,z,tex)` from Lua for temporary muzzle-flash glows? Probably yes, but out of scope for the first pass — just hand-author them in `.ent` first.

### Estimate: ~4-6 hours including asset prep.

---

## Part 2: Emissive maps

### Goal

Per-material "glow" texture that adds light on top of `diffuse × lightmap`. Independent of the dynamic flashlight (should glow whether the flashlight is pointed at it or not) and independent of the baked lightmap's darkness.

### MTL syntax

```
newmtl monitor
map_Kd textures/monitor_d.bmp
# lm_map levels/test_level_monitor_lm.bmp
# tile_scale 1.0
# emissive_map textures/monitor_e.bmp
# emissive_scale 1.2
```

- `emissive_scale` (optional, default 1.0) multiplies the emissive texture's RGB — useful for pushing a subtle glow brighter without re-exporting the texture.
- Emissive uses the **same tiling UV** as diffuse (box-mapped from world position). Separate UV sets would require a second `vt` stream, which we don't have.

### Data

Add to `Material`:
```c
char emissivePath[128];     /* empty = no emissive */
float emissiveScale;        /* default 1.0 */
```

### Two-pass render

Pass 1 is the existing `renderLevelSectored` — unchanged.

Pass 2 is a new loop immediately after, in the same function:

```c
/* ---- Emissive pass ---- */
glDepthFunc(GL_LEQUAL);          /* draw on top of identical depth */
glDepthMask(GL_FALSE);
glEnable(GL_BLEND);
glBlendFunc(GL_ONE, GL_ONE);     /* additive, no alpha */

for (each sector s) {
    Material *mat = ...;
    if (!mat || !mat->emissivePath[0]) continue;
    GLuint eTex = texCacheGet(cache, mat->emissivePath, GL_REPEAT);
    if (!eTex) continue;

    /* Single TMU: emissive only, no lightmap */
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, eTex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glColor3f(mat->emissiveScale, mat->emissiveScale, mat->emissiveScale);

    glBegin(GL_TRIANGLES);
    /* same vertex emit as pass 1, but only box-mapped diffuse UV */
    glEnd();
}

glDepthFunc(GL_LESS);
glDepthMask(GL_TRUE);
glDisable(GL_BLEND);
```

Helper refactor: extract the inner per-triangle UV/position emit into `emitSectorTrisTilingUV(mesh, sec, tileScale, offU, offV)` so both passes share it.

### Why two passes beats three TMUs

`diffuse × lightmap + emissive` in one pass needs three texture units. MX 440 has two. The options are:

| Approach | TMUs | Win98/MX440 | Draw calls | Cost |
|---|---|---|---|---|
| One pass, 3-TMU combiner | 3 | ❌ | 1× | N/A on floor |
| Two pass (chosen) | 2 | ✅ | 2× for emissive sectors only | 1 extra batch per emissive material |
| Pre-bake emissive into lightmap | 2 | ✅ | 1× | Breaks with dynamic flashlight; would need to re-bake on flashlight off; no animated glow |

Most levels will have ~2-4 emissive materials at most, so the overhead is negligible.

### Interaction with the dynamic flashlight lightmap

The flashlight writes into the lightmap RGB at the hit region each frame. In pass 1 this brightens the affected area of any textured surface. **Emissive adds on top of that regardless** — a wall monitor stays bright even when the flashlight is off and the lightmap goes dark at that texel. Exactly the behavior we want, no code needed.

### Interaction with alpha-test

A material can have both `# alpha_test` and `# emissive_map`. In pass 2 the emissive texture is usually opaque (no alpha channel) — just modulate by vertex alpha if someone supplies 32-bit emissive; default is to load RGB. Skip emissive draw for fragments that failed the alpha test in pass 1 by also enabling `GL_ALPHA_TEST` with the same threshold during pass 2.

### Estimate: ~6-8 hours including MTL plumbing, Blender material authoring notes, and one test asset.

---

## Not in scope

- Alpha-blended translucency (glass, water). Needs back-to-front sorting; covered separately once we actually have a glass asset.
- Specular highlights. Fixed-function `GL_LIGHT_MODEL_COLOR_CONTROL = GL_SEPARATE_SPECULAR_COLOR` is cheap but not needed yet.
- Light maps that animate (flicker). Orthogonal — would be a per-sector color multiplier driven from a script, independent of this work.
