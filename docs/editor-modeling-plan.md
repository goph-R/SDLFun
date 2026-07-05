# SOOB Level Editor — mesh-modeling plan

The editor is growing from an entity-placement + level-preview tool into a
**basic 3D modeler** for authoring level structure (walls, floors, ceilings)
directly, instead of round-tripping every edit through Blender. This doc is the
design + milestone plan for that.

All editor code lives under **`editor/`** — it must never mix into the game's
TU. The shared engine render path (`render_level.h`, `render_world.h`) stays at
the engine root because `main.cpp` (the game) uses it too; the editor reaches it
with a `-I.` (repo root) include, and its own headers (`edit_*.h`) sit beside
`editor.cpp` in `editor/`.

## Scope

**In:** operate on vertices / edges / faces — move them, make a face from 3–4
selected verts (Blender `F`), **extrude** selected faces (push out, stays
attached), flip normals, recalc consistent normals, delete, merge-by-distance.
Primitives (cube / plane) are just **mesh seeds** (Blender "Add > Cube", then
edit). Per-surface material + tiling offset/scale.

**Out:** UV unwrapping (box-mapping means diffuse never needs UVs), subdivision
surfaces, booleans, curves, modifiers, n-gons (tris + quads only).

## Two restrictions that make this tractable

1. **Textures always tile (box-mapping).** `computeTilingUV` projects the
   diffuse from world space per face normal, so new geometry is textured the
   instant it exists — no unwrap, no seams, no UV editor. This removes the bulk
   of what makes a hand-rolled modeler hard.
2. **Vertices snap to 0.01 (1 cm).** Every code path that writes a vertex
   position runs it through `editSnap(x) = round(x/0.01)*0.01` first. This makes
   **vertex identity exact**: two verts either coincide or they don't (quantize
   to an integer-cm key to test), so weld/merge is exact, T-junction hairline
   cracks mostly vanish, and edits are deterministic (clean save diffs).

## Data model — indexed face set (NOT half-edge)

Persistent topological structures (half-edge / winged-edge / BMesh) are
deliberately avoided — pointer-heavy and painful under the no-C++11 constraint,
and unnecessary for this op set. Instead: shared verts + tri/quad faces, with
adjacency (edge→faces, vert→faces) computed *on demand* only for the ops that
need it (extrude, edge-mode selection).

```c
#define EDIT_SNAP 0.01f                     /* 1 cm authoring grid */
typedef struct { Vec3 pos; } EditVert;      /* pos always snapped */
typedef struct {
    int  v[4]; int nv;      /* tri (v[3]=-1) or quad                */
    int  materialId;        /* index into mats[]                    */
    Vec3 normal;            /* cached, recomputed on edit           */
} EditFace;
typedef struct {
    EditVert *verts; int numVerts, capVerts;
    EditFace *faces; int numFaces, capFaces;
    Material  mats[OBJ_MAX_MATERIALS]; int numMats;  /* engine Material, verbatim */
} EditMesh;
```

`Material` is the engine struct (`diffusePath` + `tilingScale`/`tilingOffsetX/Y`)
so texture + tiling offsets export with zero translation.

## Module layout (`editor/`, header-only static, engine convention)

Pure-data modules are GL/FLTK-free, so the hard algorithms compile and unit-test
on Linux headless (`edit_mesh_test.cpp`) — Win98/FLTK is only needed for the
final GL integration.

| Module | Responsibility | Deps |
|---|---|---|
| `edit_mesh.h` | `EditMesh` + primitives: add-vert-with-snap, add/delete face, flip, normal recompute, `editAddCube`/`editAddQuad` | pure |
| `edit_mesh_build.h` | triangulate `EditMesh` → `ObjMesh` for the renderer | obj_loader |
| `edit_ops.h` | extrude, merge-by-distance, recalc-consistent-normals, translate-selection | edit_mesh |
| `edit_select.h` | selection state (vert/edge/face mode); transient edge list + on-demand adjacency | edit_mesh |
| `edit_undo.h` | snapshot stack (deep-copy whole mesh before each op) | edit_mesh |
| `edit_io.h` | native `.lvl` save/load; OBJ+MTL export (`tile_scale`/`tile_offset`) | edit_mesh |
| `edit_pick.h` | screen-space vert/edge pick + ray-triangle face pick (`gluProject`/`gluUnProject`) | GL |
| overlay + tools | draw verts/edges/faces/highlights/gizmo; modal tool state machine; FLTK panels | in `editor.cpp` |

## The one real algorithm — extrude

```
extrudeFaces(mesh, selectedFaces):
  1. edgeUse = map(unordered edge -> count within selectedFaces)
  2. Duplicate EVERY vert used by a selected face (old->new map), snapped.
  3. Rewire selected faces to their new verts        // lifts the cap off
  4. For each boundary edge (edgeUse==1): emit a side quad wound outward
     from the edge's direction in its one selected face; inherit material.
  5. New selection = the lifted cap faces -> auto-enter Grab (normal-locked).
```

Duplicating *all* used verts keeps interior shared edges of a multi-face
selection welded while the patch detaches from surrounding geometry. Side-quad
winding is the fiddly bit; **flip** and **recalc-normals** are the safety nets.

## Interaction (Blender muscle memory, adapted)

`1/2/3` vert/edge/face mode · click select, Shift+click add · **G** grab (then
`X/Y/Z` axis-lock, live 1 cm snap, click/Esc confirm/cancel) · **E** extrude→grab
· **F** make face from 3–4 verts · **M** merge · **Del** delete · flip /
recalc-normals on toolbar+key · **Ctrl+Z / Ctrl+Y** undo/redo · **A** all/none.
A small modal state machine in `editor.cpp` drives it; box-select + gizmos are
polish-tail.

## Formats

- **Native `.lvl`** — text: `snap 0.01`, `material` lines, `v x y z`,
  `f i j k [l] mat N`. Hand-editable, diff-clean.
- **Export OBJ + MTL** — standard OBJ; MTL carries the `tile_scale`/`tile_offset`
  comments the engine already parses. Weld + recalc-normals on export. The
  **Blender lightmap bake pipeline is unchanged** — it re-unwraps, so editor-mesh
  vs bake-mesh never conflict.

## Milestones

| M | Deliverable | Status |
|---|---|---|
| **M0** | `edit_mesh.h` + `edit_mesh_build.h`; a hardcoded snapped cube renders box-mapped through the engine path. | **done** |
| **M1** | Selection + overlay + picking: click verts/edges/faces, they highlight; modes 1/2/3. | **in progress** |
| M2 | **Grab** with axis-lock + live 1 cm snap; **snapshot undo**. | |
| M3 | **Make-face (F)**, flip, delete, **Add Cube/Plane** seeds. | |
| M4 | **Extrude** + merge-by-distance + recalc-normals. *The core verb.* | |
| M5 | Materials panel: per-face assign + tiling spinners, live. | |
| M6 | `.lvl` save/load + OBJ/MTL export; round-trip a Blender bake. | |
| polish | Gizmos, box-select, loop-select, numeric entry, snap toggle. | |

## Spine

Indexed face set + on-demand adjacency + snapshot undo + snap-on-every-write;
primitives as mesh seeds; extrude as the one real algorithm; box-mapping meaning
zero UV work. M0 → M4 is the real journey; M5–M6 reuse pieces already in place.
