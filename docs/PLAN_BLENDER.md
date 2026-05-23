# SOOB Engine — Blender Export Addon

Single source of truth for the Blender plugin. Supersedes the Blender
sections in `PLAN_SECTORS.md` (Phase 2) and `PLAN_ENTITIES.md` (Phase D),
which predate most of what the engine actually ships today.

## Context

The engine is now far enough along that hand-authoring `.ent` files, hand-
running OBJ exports, and hand-baking PNG lightmaps is the slowest part of
making content. We have:

- Multi-material level OBJ + MTL with custom `# lm_map`, `# tile_scale`,
  `# tile_offset`, `# alpha_test` properties (PNG only, see
  `docs/level-design.md`).
- 11 entity types: `player`, `decoration`, `item`, `enemy`, `platform`,
  `switch`, `trigger`, `door`, `waypoint`, `path_node`, plus the implicit
  level geometry. Schema lives in `entity.h` (see `entLoadFile`).
- Recent additions the old plans don't mention: doors with
  `motion=slide|rotate`, path-following platforms with `path=<group>
  move=once|ping_pong enabled= face_path=`, `path_node` ordering,
  `waypoint` nav graph nodes.
- Asset registry — `.ent` files reference models/textures by short
  logical names from `assets.lua`, falling through to raw paths if
  unknown.

This plan reconciles the addon to the current entity schema and naming,
and drops dead-end ideas (BMP, `dir_light` / `point_light` entities, the
old start_y/end_y platform schema).

## Naming

Project name: **SOOB Engine** — SDL + OpenGL + OpenAL + Bullet.

Blender custom-property prefix: `soob_`. The N-panel header reads
"**SOOB Entity**". The addon's `bl_info["name"]` is "SOOB Level
Exporter" and the file is `soob_export.py`.

Why a prefix at all: custom properties live in the same flat namespace as
every other addon's properties, so prefixing keeps us from colliding with
Blender's built-ins or another addon someone might be running.

## Layout

Single-file Python addon at `tools/soob_export.py` (the existing `tools/`
directory already holds `bmp_tga_to_png.py` and `gen_sounds.c`, so this
fits the convention). Estimated ~800–1200 lines once the entity property
groups, viewport overlays, and `.ent` writers are in.

### Module sections

```
soob_export.py
├── bl_info, register/unregister
├── Property groups (one per entity type, mirrors entity.h)
├── UI panels (Object Properties > SOOB Entity)
├── Viewport overlays (gpu module — path lines, trigger boxes, icons)
├── Lightmap bake helpers
├── OBJ + MTL writer (custom — adds `# lm_map`, `# tile_scale`)
├── .ent writer (one function per entity type)
├── Asset copy helpers
└── Export operator (File > Export > SOOB Level)
```

Header-only-style single TU. No external Python deps beyond what
Blender 3.x bundles (`bpy`, `bmesh`, `gpu`, `mathutils`).

## Entity classification

Each Blender object has a custom property **`soob_type`** that picks one
of:

| `soob_type` value   | Engine type      | Notes |
|---------------------|------------------|-------|
| *unset*             | level geometry   | Joined into the OBJ + lightmap bake |
| `player`            | `ENT_PLAYER`     | Empty (arrows display). Spawn point. |
| `decoration`        | `ENT_DECORATION` | Mesh or IQM; static or animated. |
| `item`              | `ENT_ITEM`       | Empty or mesh. |
| `enemy`             | `ENT_ENEMY`      | Empty; IQM character. |
| `platform`          | `ENT_PLATFORM`   | Mesh. First in group with `path=` set is the leader. |
| `switch`            | `ENT_SWITCH`     | Mesh. |
| `trigger`           | `ENT_TRIGGER`    | Empty; box display scaled to `size=` |
| `door`              | `ENT_DOOR`       | Mesh. `motion=slide|rotate`. |
| `waypoint`          | `ENT_WAYPOINT`   | Empty; nav graph node. |
| `path_node`         | `ENT_PATH_NODE`  | Empty; grouped + ordered for platform paths. |

The "level geometry" case (no `soob_type`) is the most important: an
artist can just model walls/floors as usual, and only opt-in to entity
behavior on the few objects that need it. The default workflow stays
"select your level mesh, hit export".

## Property groups (mirror of `entity.h`)

The addon registers one `PropertyGroup` per entity type, attached to
`bpy.types.Object` under `obj.soob`. Fields are 1:1 with the `entLoadFile`
parser keys in `entity.h:280-379`:

```python
class SoobCommon(PropertyGroup):
    type    : EnumProperty(...)         # the soob_type pick above
    name    : StringProperty(default="")  # blank = derive from obj.name
    group   : StringProperty(default="")
    collide : EnumProperty(items=[("none","None",""),
                                  ("box","Box AABB",""),
                                  ("trimesh","Trimesh","")])
    scale       : FloatProperty(default=1.0)
    static      : BoolProperty(default=False)
    flip_cull   : BoolProperty(default=False)
    # Visual binding
    mesh        : StringProperty(description="assets.lua logical name, blank = inline")
    tex         : StringProperty(description="assets.lua logical name")
    iqm         : StringProperty(description="assets.lua logical name, animated")
    anim        : StringProperty(description="initial anim name")
    anim_speed  : FloatProperty(default=1.0)

class SoobPlatform(PropertyGroup):
    path        : StringProperty(description="path_node group name")
    move        : EnumProperty(items=[("once","Once",""),
                                      ("ping_pong","Ping-pong","")])
    speed       : FloatProperty(default=1.5)
    enabled     : BoolProperty(default=True)
    face_path   : BoolProperty(default=False)

class SoobTrigger(PropertyGroup):
    size_x : FloatProperty(default=1.5)
    size_y : FloatProperty(default=2.0)
    size_z : FloatProperty(default=1.5)
    target : StringProperty()
    once   : BoolProperty(default=False)

class SoobDoor(PropertyGroup):
    motion     : EnumProperty(items=[("slide","Slide"),("rotate","Rotate")])
    axis       : EnumProperty(items=[("X","X"),("Y","Y"),("Z","Z")])
    amount     : FloatProperty(default=1.0)
    speed      : FloatProperty(default=1.0)
    auto_close : FloatProperty(default=3.0)

class SoobPathNode(PropertyGroup):
    order : IntProperty(default=0)

# ...similar small groups for item, enemy, switch
```

### N-panel ("Object Properties > SOOB Entity")

```
[ SOOB Entity ]
  Type:        [Platform        ▼]
  Name:        [lift1_floor      ]   (blank = use object name)
  Group:       [lift1            ]
  Collide:     [Box AABB         ▼]
  Scale:       [1.0   ]
  Static:      [ ]   Flip cull: [ ]

  -- Visual --
  Mesh:        [door             ]  (logical name from assets.lua)
  Texture:     [wood1            ]
  IQM:         [                 ]

  -- Platform --     (visible when Type=Platform)
  Path group:  [lift1_path       ]
  Move:        [Ping-pong       ▼]
  Speed:       [1.5   ]
  Enabled:     [ ]
  Face path:   [✓]
```

Type-specific subpanels show/hide via `bl_label` + a `poll` classmethod
that checks `obj.soob.common.type`. Standard Blender pattern, no surprises.

## Viewport overlays

Custom GPU draw handler registered on `SpaceView3D`. All overlays read
from object properties, so they update live as the user edits values.
Toggle via `View > Overlays > SOOB Entities` and an N-panel checkbox.

| Entity      | Drawing |
|-------------|---------|
| `player`    | Blue crosshair at origin, small forward arrow |
| `trigger`   | Yellow translucent box, edges of size `(size_x, size_y, size_z)` |
| `enemy`     | Red wireframe capsule (1.8m tall, 0.4m radius) at origin |
| `item`      | Green wireframe diamond |
| `platform`  | Orange wireframe AABB derived from the mesh |
| `door`      | Purple wireframe AABB + arrow showing open direction |
| `switch`    | Cyan wireframe circle |
| `waypoint`  | Small white sphere |
| `path_node` | Magenta octahedron (matches `pathDebugRender` in-game) |

**Path-group polylines.** For each unique `path` group name, collect all
`path_node` objects, sort by `order`, draw a cyan polyline through them.
Draw a magenta forward arrow on the first segment from each leader-
platform's pivot toward node 0. Mirrors the in-game `pathDebugRender`
exactly so the in-Blender layout looks the same as `B`-key debug viz
in-game.

## Export pipeline

`File > Export > SOOB Level (.obj)` operator. Dialog:

| Field | Default | Notes |
|-------|---------|-------|
| Level name | scene name | becomes `<name>.obj`/`.ent` |
| Export path | last used | writes under this directory |
| Lightmap resolution | 512 | per-sector lightmap atlas size |
| Lightmap UV map name | `Lightmap` | which UV map goes into OBJ `vt` |
| Auto-create lightmap UVs | ✓ | Smart UV Project (margin 0.04) if missing |
| Bake lightmaps | ✓ | uncheck for fast geometry-only export |
| Copy referenced assets | ✓ | resolves `mesh=`/`tex=`/`iqm=` against assets.lua and copies the source files |

### Steps

1. **Classify** every object:
   - `soob_type` unset and is a mesh → level geometry candidate
   - `soob_type` set → entity, recorded with world transform
   - Anything else (lights, cameras, empties without `soob_type`) → skipped
2. **Prepare level geometry**:
   - Duplicate-link the candidates into a temp scene
   - Join into one mesh
   - Ensure a UV map named `Lightmap` exists (Smart UV Project, margin
     0.04 — see `docs/level-design.md`)
3. **Bake lightmaps** (if enabled):
   - Render engine = Cycles, samples configurable (default 64)
   - For each material on the joined mesh: create image at the chosen
     resolution, add an Image Texture node bound to it (selected, not
     connected), bake `Diffuse > Direct + Indirect`, save as
     `<material>_lm.png`
   - Include `is_static=True` decorations in the bake scene so they
     cast / receive light
4. **Write OBJ + MTL**:
   - Custom writer (not the built-in operator — we need precise control
     over `# lm_map`, `# tile_scale`, `# tile_offset` comments)
   - `v`, `vt` (from `Lightmap` UV), `vn`, `f` with `mtllib` /
     `usemtl` switches at material boundaries
   - MTL: `newmtl`, `map_Kd`, `# lm_map`, `# tile_scale` (read from the
     material's Mapping node Scale.x, falling back to a per-material
     `soob_tile_scale` custom property), `# tile_offset`
5. **Copy diffuse textures**:
   - For each material, find the Image Texture node feeding Base Color
   - If it's already on disk as a PNG, copy as-is; otherwise save the
     image data block to PNG
   - For materials whose diffuse is registered in `assets.lua`, leave the
     existing file alone and reference its `assets/textures/...` path
     from the MTL
6. **Write `.ent`**:
   - For each entity object, format one line using the schema in
     `entity.h:280-379`. World transform → `posX posY posZ rotY`. Group
     blank → `-`. Type-specific keys only when set / non-default.
   - Path nodes emit in object name order; `order=` is taken from
     `soob.pathnode.order`.
7. **Copy referenced assets** (if enabled):
   - Walk every entity's `mesh=`/`tex=`/`iqm=` value
   - If it's a logical name in `assets.lua`, copy the source file into
     `assets/models/` or `assets/textures/` only if the destination
     doesn't already exist (don't clobber registered assets)
   - If it's a raw path, copy it as-is preserving the relative path

### Output paths

Default export root is the repo root (so re-running the addon overwrites
`assets/levels/test_level.*` in place). The dialog can point elsewhere
for a sandboxed export.

```
<export_root>/
  assets/levels/<name>.obj
  assets/levels/<name>.mtl
  assets/levels/<name>.ent
  assets/levels/<material>_lm.png           (one per material)
  assets/textures/<diffuse>.png              (only if copying)
  assets/models/<mesh-or-iqm>                (only if copying)
```

## `.ent` writer schema

The writer mirrors the parser at `entity.h:247-423` line-for-line. For
every type, the format is:

```
<type> <name> <group> <posX> <posY> <posZ> <rotY> [key=value ...]
```

`<name>` and `<group>` collapse to `-` when blank. Floats use `%.3f` with
trailing-zero trim. Keys are emitted only when non-default to keep the
file diff-friendly:

```python
def write_platform(obj, e):
    keys = []
    if e.mesh:        keys.append("mesh=" + e.mesh)
    if e.tex:         keys.append("tex=" + e.tex)
    if e.collide!=0:  keys.append("collide=" + ("box","trimesh")[e.collide-1])
    if p.path:        keys.append("path=" + p.path)
    if p.move != "once": keys.append("move=" + p.move)
    if p.speed != 1.5:   keys.append("speed=%.2f" % p.speed)
    if not p.enabled:    keys.append("enabled=0")
    if p.face_path:      keys.append("face_path=1")
    return "platform %s %s %s %.3f %.3f %.3f %.1f %s\n" % \
           (name, group, x, y, z, rot_y, " ".join(keys))
```

One writer per type (`write_player`, `write_decoration`, `write_door`,
etc.). Round-trip with `assets/levels/test_level.ent` is the
acceptance test — exporting the imported version should produce an
identical file (modulo whitespace).

## Import path (round-trip)

Phase 2 nice-to-have: a matching importer (`File > Import > SOOB
Level`) that reads `<name>.obj`/`.ent`, rebuilds the Blender scene
with the right `soob_type` and properties on every object, and joins
level geometry into one mesh. Lets us bootstrap editing existing
levels in Blender. Not required for v1 of the addon but the data
model is designed so a writer's output is a valid round-trip target.

## Coordinate handling

Blender is Z-up, right-handed. The engine OBJ pipeline already uses
"-Z Forward, Y Up" on Blender's side (see `docs/level-design.md` step
6), which maps Blender → engine cleanly. The export operator forces
these axes, no UI for it.

`rotY` in the `.ent` file is engine Y-axis rotation in degrees. From
Blender that's `degrees(obj.rotation_euler.z)` after the axis swap (Z
in Blender becomes Y in the engine). The other Euler axes are
deliberately discarded — entities are gravity-aligned in the engine.

For `trigger` objects, `size=sx,sy,sz` is read from `obj.scale * 2`
(matching the wireframe-box overlay convention: scale 1 = a 2m cube).

## Implementation phases

Same incremental approach we used for path platforms — each step
builds something testable.

1. **Skeleton**: `bl_info`, `register/unregister`, empty operator under
   `File > Export > SOOB Level`. Verify the addon installs and shows up.
2. **Common property group** + N-panel with `type` dropdown only.
   Verify the panel appears on objects and the dropdown sticks.
3. **OBJ + MTL writer** (no bake, no entities). Take the active mesh,
   write `<name>.obj` + `.mtl` with `# tile_scale` etc. Diff against
   `assets/levels/test_level.obj`.
4. **Lightmap bake** — per-material bake driven by `objBuildSectors`-
   compatible material assignments. Bake `test_level.blend` and verify
   the in-engine render matches the hand-baked version.
5. **Entity property groups** + type-specific subpanels. Hook up all 11
   types but emit nothing yet.
6. **`.ent` writer** — one type at a time, validate against
   `entLoadFile` by running the engine after each.
7. **Viewport overlays** — path polylines first (most useful), then
   trigger boxes, then per-type icons.
8. **Asset copy** — opt-in pass that walks `assets.lua` and copies
   referenced files only when missing at the destination.
9. **Round-trip** — manual test: export the current `test_level.blend`
   to a sandbox dir, run the engine pointed at it, verify identical
   behavior to in-tree assets.
10. **Importer** (optional, Phase 2): read `.obj`/`.ent` back into
    Blender as a scene with `soob_*` properties populated.

Build verification after each step is "the engine still loads
`test_level.ent` and runs". The addon itself has no compile step — it
loads or it doesn't.

## Files touched / created

- `tools/soob_export.py` — new, the addon itself
- `docs/PLAN_BLENDER.md` — this file
- `docs/level-design.md` — add a "SOOB exporter" section at the top
  pointing at the addon, leaving the manual workflow as a fallback
- `docs/PLAN_SECTORS.md` — one-line "see PLAN_BLENDER.md" note at the
  top of Phase 2, leave the rest as historical record
- `docs/PLAN_ENTITIES.md` — same one-line redirect at the Phase D
  section

## Open questions

1. **Inline OBJ entity meshes vs. logical asset names**. Today
   decorations either reference a registered asset (`mesh=office_desk`)
   or a raw path. Should the addon also support exporting a unique
   per-object mesh inline next to the level (e.g.,
   `assets/levels/<name>_meshes/lift1_floor.obj`)? Probably yes
   eventually; defer for v1.
2. **Material → tile_scale source**. Blender artists set tiling via the
   Mapping node `Scale.x` value (see `docs/level-design.md:45`). Read
   from there primarily, with a per-material `soob_tile_scale` custom
   property as an override. Decide which one wins on conflict —
   recommendation: the custom property wins, matches "explicit beats
   implicit".
3. **PNG vs. raw image data**. Blender images can be packed, generated,
   or external. The writer should `image.save_render(filepath, ...)` for
   anything not already a PNG on disk. Verify that 32-bit RGBA inputs
   stay 32-bit through this path (cutout textures depend on it — see
   `# alpha_test` in `docs/level-design.md:178`).
4. **`anim_speed` default**. Engine treats 0 as "no playback" (see
   `entUpdate` near `entity.h:434`). The addon's default should be
   `1.0` so unset values still animate.
5. **`path_node` `order=`**. Authors will forget to set it. Default to
   sorting by object name (`p0`, `p1`, `p2` ...) if all `order` values
   in a group are zero, but warn loudly on export. Better long-term:
   bake `order` into the property when the user reorders the nodes in
   Outliner — defer to v2.

## Risks

- **Bake reliability across Blender versions.** Cycles bake API has
  shifted between 2.8 and 4.x. Pin tested versions in the addon header
  and `bl_info["blender"]`.
- **Custom GPU draw handlers are version-sensitive.** Blender 3.x→4.x
  changed `gpu_extras.batch.batch_for_shader` semantics slightly. Test
  on the project's current Blender (currently used by the artist for
  lightmap bakes; verify version before starting Step 7).
- **Round-trip diffs**. OBJ float precision and material ordering vary
  between Blender's built-in exporter and ours. We deliberately roll
  our own writer to keep the output deterministic and diff-friendly —
  worth re-asserting in CI/CD if we add tests later.
- **Asset copy can clobber registered assets.** Mitigate by only
  copying to `assets/models/` or `assets/textures/` when the
  destination file doesn't already exist (see step 7).

## Non-goals

- Animating IQM models (use external pipeline — see
  `docs/mixamo-iqm.md`)
- Editing `assets.lua` from the addon (the file stays hand-edited)
- Building / packaging the engine (`build.bat` etc. stay separate)
- Importing existing `.ent` files (Phase 2 — see "Import path" above)
- Anything that requires Blender ≥ 4.x exclusive features unless we
  also gate behind a version check
