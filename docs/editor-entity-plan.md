# SOOB Level Editor — entity mode plan

A fourth selection mode — **entity** — alongside vertex / edge / face. In it you
**select** entities (single + multi), **move** and **rotate** them, **add** new
ones per type, **delete** the selected ones, and edit their fields through a
**per-type property form** (position X/Y/Z + rotation + the type-specific
fields). This doc is the design + milestone plan, and is the companion to
`editor-modeling-plan.md` (which covers the geometry-modeling side).

## Where this sits relative to what already exists

The editor already **loads and renders** a level's entities as a live preview
(`scene.entities`, an `EntityList*`, drawn by `entRender` inside `renderWorld`),
but nothing edits them — they are read-only today. So entity mode is a genuinely
new editable document layered beside the existing `emesh` geometry document.

Reusable as-is (the good news):

- **Ray + projection math** — `edit_pick.h` already has `editMakeRay`,
  `editProject`, `editDepth`, `editRayTri`. Entity picking is a small addition.
- **The grab modal** — `grabStart/Gather/Apply/Confirm/Cancel` with axis-lock,
  1 cm snap, and the pixel→world delta math is entity-agnostic; we branch it.
- **The property-panel pattern** — overlapping `Fl_Group`s shown one-at-a-time by
  `refreshPanel`, with the `vertGroup` read (`refreshVertPanel`) / write
  (`onVertPosChanged`, guarded by `vertEditPushed` for one-undo-per-gesture) as a
  direct template for an entity form.
- **`focusPoint()`** — a snapped world point 5 m in front of the camera, already
  used to drop new cubes/planes; reused verbatim to spawn new entities.
- The mode plumbing is uniform: an enum value, per-element flag arrays in
  `EditSelection`, a repeated mode-dispatch `if/else`, and one radio button per
  mode. A fourth slots in cleanly.

Three gaps that are net-new work (the honest news):

- **No `.ent` writer exists anywhere** (`entLoadFile` is load-only; the editor's
  `edit_io.h` writes geometry only). Saving edited entities is from scratch.
- **The `Entity` struct is lossy** — `entLoadFile` resolves `mesh=`/`tex=`/`iqm=`/
  `anim=` into loaded data and **discards the authored names**. We can't re-emit
  `mesh=office_desk` without them. Fixing this is a prerequisite for both the
  form and the writer.
- **No rotate exists** — there is no `R` handler or rotate function in the whole
  editor. Rotate is built from scratch (the grab modal is the pattern to clone).
- **Undo is `EditMesh`-only** and the entity list is ~4 MB (each `Entity` embeds
  an `ObjMesh` + `IqmModel` inline), so it can't be deep-copied per edit.

## Entity data model (what we're editing)

11 existing types (`entity.h`): `player, decoration, item, enemy, platform,
switch, trigger, door, waypoint, path_node` (+ `none`). The engine has **no light
type** today — lighting is baked lightmaps + the runtime flashlight. This plan
**adds a 12th type, `light`** (see D8): a position-only *sphere light* with
color / intensity / radius, authored, visualized, and saved by the editor. Like a
waypoint it has no mesh, and the game runtime ignores it for now — its consumer is
the downstream Blender lightmap bake.

Common fields every entity has: `name[32]`, `group[32]`, `posX/posY/posZ`,
`rotY` (yaw degrees — the only rotation axis), `scale` (uniform). Then a
type-tagged union:

| Type | Type-specific fields (`.ent` keys) |
|---|---|
| player | — (transform only; exactly one, tracked by `playerIndex`) |
| decoration | — (mesh/anim via common `mesh=`/`iqm=`/`tex=`) |
| item | `item_type` (0 health / 1 ammo / 2 key) |
| enemy | `health`, `speed`, `sight` |
| platform | `path`, `move` (once/ping_pong), `speed`, `enabled`, `face_path` |
| switch | `target` |
| trigger | `size` (x,y,z), `target`, `once` |
| door | `motion` (slide/rotate), `axis`, `amount`, `speed`, `auto_close` |
| waypoint | — (nav node, position-only, no mesh) |
| path_node | `order` (+ uses `group` for the path name) |
| light *(new)* | `color` (r,g,b), `intensity`, `radius` — sphere light; position-only, no mesh (D8) |

Plus the common asset keys (`mesh`, `tex`, `iqm`, `anim`, `scale`, `static`,
`flip_cull`, `anim_speed`, `collide`). `.ent` line grammar (one entity per
physical line):

```
type name group posX posY posZ rotY [key=value ...]
```

`name`/`group` of `-` means none. The editor holds entities in a fixed inline
array `EntityList.entities[MAX_ENTITIES]` (256), so **entity index == slot** is
stable — we can key selection and undo off the raw slot index.

## Design decisions

**D1 — Selection state: a fixed flag array.** Add `unsigned char
entSel[MAX_ENTITIES]` + `int numEnts` to `EditSelection` (or a parallel struct).
Because `MAX_ENTITIES` is a fixed 256, size it once (256 bytes) and never resize
— add/delete just flip `active`, no reallocation. Multi-select is native (same
per-element-flag model as verts/faces). Shift-click toggles, plain click
replaces — identical idiom to the mesh picks.

**D2 — Stop discarding asset names (engine change, prerequisite).** Add
`char meshName[32], texName[32], iqmName[32], animName[32]` to `Entity` and have
`entLoadFile` store the authored tokens alongside the resolved data. Cost:
~128 B × 256 = 32 KB on a 4 MB list — negligible, purely additive, the game
ignores them. Without this we cannot round-trip `mesh=`/`tex=`/`iqm=`/`anim=` in
either the form or the writer. This is the one change outside `editor/`.

**D3 — Entity-aware undo via a compact tagged snapshot.** Generalize
`EditHistory` to hold **tagged** snapshots — each entry is either a mesh copy
(as today) *or* a compact entity snapshot — so a single Ctrl+Z ordering spans
both documents and mesh copies stay lean. The entity snapshot copies **only the
editable POD** (active, type, name, group, transform, scale, union, the D2 name
fields) and **excludes the embedded `ObjMesh`/`IqmModel`/`physBody`** — those are
heavy and never change under move/rotate/field-edit. To keep them stable across
undo, **delete = mark `active=0`** (don't free the mesh); undo of a delete just
flips it back. Add loads a mesh once; undo of an add marks inactive (mesh stays
resident for redo). Snapshot size ≈ 300 B × 256 ≈ 77 KB, ×64 deep ≈ 5 MB — fine.

**D4 — Rotate is yaw-only (`R` modal).** Clone the grab modal into a rotate
modal on `R`. Since `rotY` is the only axis, rotate is 1-DOF: mouse-X delta →
yaw delta, snapped (e.g. 15° default, 1° with a modifier). Single selection
rotates about the entity's own origin (just `rotY += d`). Multi-selection rotates
about the **selection centroid** — each entity's position orbits the centroid in
XZ *and* its `rotY += d` (the Blender behavior). Reuses the grab confirm/cancel
+ undo scaffolding.

**D5 — One form, fields shown by type.** A single `entGroup` panel builds *all*
possible widgets once (name, group, type label, X/Y/Z, RotY, Scale, then every
type-specific field), and `refreshEntPanel` shows/hides the relevant subset from
the selected entity's `type`. Shown only when **exactly one** entity is selected
(mirrors `vertGroup`'s n==1 rule); multi-select relies on grab/rotate/delete and
shows just a count. Live write-back with a `entEditPushed` one-undo-per-gesture
guard, same as vertices. Mesh-bearing types get a `mesh=`/`tex=` field that
**reloads** the entity's `ObjMesh`/texture on change (re-resolve through the
AssetRegistry + `objLoad`/`texCacheGet` — all available in the editor).

**D6 — Saving writes `.ent` next to geometry.** New `editSaveEnt(EntityList*,
path)` emits the `.ent` grammar (inverting the key map, one entity per physical
line, `-` for empty name/group, skipping `active==0` slots). `File > Save`
writes it to the loaded `entPath` alongside the `.lvl`/OBJ geometry. Entities
stay in the separate `.ent` file (that's what the engine loads) — not embedded in
`.lvl`. A round-trip test (load → edit → save → reload → compare) is the
acceptance gate.

**D7 — Add spawns at the camera focus.** An `Add Entity` menu with one item per
type. New entity is created at `focusPoint()` with sane defaults, selected, and
dropped straight into grab (like Add Cube). Guard: adding a second `player`
warns/reuses; deleting the `player` warns.

**D8 — Light entities (sphere lights).** Add `ENT_LIGHT` to the engine
`EntityType` and a union member `struct { float r, g, b; float intensity; float
radius; } light;` — color in 0..1, intensity a scalar multiplier, radius the
sphere size in metres. `entLoadFile` gains three keys: `color=r,g,b`
(`sscanf "%f,%f,%f"`, mirroring the trigger `size=` key), `intensity=`,
`radius=`; defaults white / `1.0` / `4.0`. Position-only with no mesh, so the
game runtime renders/does nothing with it — ignored exactly like a waypoint — and
the authored lights flow downstream to the Blender bake. This is a **second
engine-side change and lands together with D2**; the D2 asset-name fields are
unused for lights (they reference no assets).

- *Editor visualization.* Lights have no mesh, so the editor draws a **wireframe
  sphere** at the light's `radius` — three great-circle `GL_LINE_LOOP`s in the
  XY / YZ / XZ planes (cheap fixed-function), tinted by `color * intensity`, plus
  a bright origin marker. Selection brightens/thickens it. This gives authoring
  feedback on position and reach without any real-time GL lighting.
- *Form (EM3).* Color edited as three 0..1 `Fl_Value_Input`s (R/G/B) — guaranteed
  to build — optionally fronted by a color-swatch button opening `fl_color_chooser`
  **iff** `Fl_Color_Chooser` is in the vendored core lib (to verify; falls back to
  the numeric inputs — same "check the link line first" caution as the PNG /
  `fltk_images` lesson). Plus `intensity` and `radius` inputs.
- *Out of scope (for now).* Driving live `GL_LIGHT0..7` from these entities for a
  lit viewport — the engine's lighting is baked, fixed-function GL caps at 8
  lights, and a GL preview wouldn't match the Cycles bake. The wire-sphere gizmo
  is the authoring feedback; a live-preview pass can be added later if wanted.

## Module layout

| File | Change |
|---|---|
| `entity.h` (engine) | **D2** + **D8**: add `meshName/texName/iqmName/animName`, *and* `ENT_LIGHT` + its `light` union + `color`/`intensity`/`radius` parsing. The engine-side changes, landing together. |
| `editor/edit_select.h` | `SEL_ENTITY = 3`; `entSel[]`/`numEnts`; extend init/free/clear (make ENTITY explicit — the current final `else` aliases FACE). |
| `editor/edit_pick.h` | `editPickEntity` — clone `editPickVertex`, iterate active entities, project origin, nearest within radius (AABB ray-test optional later). |
| `editor/edit_ent_io.h` *(new)* | `editSaveEnt` writer (**D6**). |
| `editor/edit_undo.h` | tagged snapshot generalization + compact entity snapshot (**D3**). |
| `editor/editor.cpp` | mode button/icon/key `4`; `applyMode`/`modeButtonCb`/`refreshPanel`/`doPick` ENTITY branches; entity grab + rotate; `entGroup` + read/write (incl. light color/intensity/radius); light **wire-sphere** overlay draw; Add/Delete; guard mesh-only ops in entity mode. |
| `editor/icons/mode_entity.xpm` *(new)* | toolbar icon (same 16×16 XPM convention). |

## Milestones

| M | Deliverable | Notes |
|---|---|---|
| **EM0** | **Mode + pick + highlight.** `SEL_ENTITY` plumbed end to end (enum, flag array, button, icon, key `4`, all dispatch branches explicit). Click selects an entity (origin pick), shift = multi-select; selected entities drawn highlighted (AABB wire / origin marker) in the overlay pass. Mesh-only ops (E/F/M/N) guarded to no-op in entity mode. | No mutation yet → no undo needed. |
| **EM1** | **Move + entity undo.** Branch grab for entities (move selected origins by the snapped, axis-lockable delta; multi moves together). Lands the **D3** tagged/compact undo — the first entity mutation. | Undo generalization ships here. |
| **EM2** | **Rotate (`R`).** Yaw-only rotate modal (**D4**): single about own origin, multi about centroid. | Reuses EM1 undo. |
| **EM3** | **Property form.** `entGroup` with common + per-type fields (**D5**); live read/write with undo guard. Requires **D2** for the mesh/tex/iqm/anim fields; mesh reload-on-change. | |
| **EM4** | **Add / Delete.** `Add Entity` submenu → spawn at focus → grab (**D7**); Delete selected (mark inactive, undoable). Player-uniqueness guard. | |
| **EM5** | **Save `.ent`.** `editSaveEnt` (**D6**); `File > Save` writes entities to `entPath`; load→edit→save→reload round-trip is clean. | Acceptance gate. |

**Lights (D8) are just the 12th type**, threading through the same milestones:
selectable + wire-sphere in EM0, movable in EM1, the color/intensity/radius form
in EM3, `Add Light` in EM4, and `color`/`intensity`/`radius` keys in EM5's
writer. The only light-specific code beyond the per-type branches is the
wire-sphere overlay and the engine type itself (D8, paired with D2). Rotate (EM2)
is a no-op on a sphere light, so it's simply skipped for that type.

## Spine

A fixed 256-slot selection flag array (slot == identity) + entity picking as
screen-space origin distance + the existing grab modal branched for entities +
a new yaw-only rotate + one type-driven form + a compact tagged undo + a `.ent`
writer, plus a new `light` type drawn as a wire-sphere gizmo. Two engine-side
changes ride together: **stop discarding asset names** (D2) and **add `ENT_LIGHT`**
(D8). EM0→EM2 is the interaction core; EM3–EM5 reuse the form/undo/IO patterns
already proven on the geometry side.
