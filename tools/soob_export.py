"""SOOB Engine — Blender level exporter.

One-click export of level geometry, lightmaps, and entities to the
engine's OBJ + MTL + .ent format. See `docs/PLAN_BLENDER.md` for the
full design.

Installation:
    Blender > Edit > Preferences > Add-ons > Install...
    Pick this file, enable "SOOB Engine - Level Exporter".
    The export option appears under File > Export > SOOB Level (.obj).

Status: feature-complete on the v1 roadmap (Steps 1-9 of
docs/PLAN_BLENDER.md). Exports level geometry, per-material Cycles
lightmaps, entities (.ent), and copies referenced assets. Viewport
overlays show path-group polylines, leader arrows, trigger boxes,
and path-node markers in the 3D View, toggled via the SOOB N-panel.
"""

bl_info = {
    "name":        "SOOB Engine - Level Exporter",
    "author":      "Dynart",
    "version":     (0, 1, 0),
    "blender":     (3, 0, 0),
    "location":    "File > Export > SOOB Level (.obj)",
    "description": "Export SOOB Engine level (OBJ + MTL + lightmaps + .ent)",
    "category":    "Import-Export",
}

import os

import bpy
from bpy.types import Operator, Panel, PropertyGroup
from bpy.props import (
    EnumProperty, PointerProperty, StringProperty, BoolProperty,
    IntProperty, FloatProperty,
)
from bpy_extras.io_utils import ExportHelper


# ---------------------------------------------------------------------------
# OBJ + MTL writer
# ---------------------------------------------------------------------------
#
# Custom Wavefront writer (we don't use Blender's built-in OBJ operator
# because we need precise control over the # lm_map / # tile_scale /
# # tile_offset comments the engine parser reads, plus deterministic
# output for diff-friendly round-trips).
#
# Coordinate convention: Blender is Z-up right-handed; engine is Y-up
# right-handed with -Z forward. Per the existing pipeline (see
# docs/level-design.md step 6), the swap is:
#
#     engine X =  blender X
#     engine Y =  blender Z
#     engine Z = -blender Y
#
# Normals get the same swap. UVs pass through unchanged — the engine's
# objLoad flips V on load (obj_loader.h:263), so we write Blender's
# UV directly.
#
# Geometry is taken from the *evaluated* mesh (modifiers applied) of
# the active object, transformed into world space. calc_loop_triangles()
# gives us a triangulated view without mutating the source mesh.

def _swap_axes(v):
    """Blender (x, y, z) -> engine (X=x, Y=z, Z=-y)."""
    return (v[0], v[2], -v[1])


def _lightmap_uv_layer(mesh, preferred_name):
    """Return the UV layer to use for `vt` output.

    Looks for the preferred name first, then any active UV layer, then
    the first available layer. None means the mesh has no UVs at all.
    """
    if not mesh.uv_layers:
        return None
    layer = mesh.uv_layers.get(preferred_name)
    if layer is not None:
        return layer
    if mesh.uv_layers.active is not None:
        return mesh.uv_layers.active
    return mesh.uv_layers[0]


def _material_tile_scale(mat):
    """Resolve per-material tile_scale.

    Order of precedence (open question #2 in PLAN_BLENDER.md):
      1. `soob_tile_scale` custom property on the material (explicit)
      2. Mapping node Scale.x in the material's node tree (Blender UI)
      3. Default 1.0
    """
    if mat is None:
        return 1.0
    if "soob_tile_scale" in mat:
        try:
            return float(mat["soob_tile_scale"])
        except (TypeError, ValueError):
            pass
    if mat.use_nodes and mat.node_tree is not None:
        for node in mat.node_tree.nodes:
            if node.type == "MAPPING":
                # inputs[3] is "Scale" in shader Mapping node
                try:
                    return float(node.inputs["Scale"].default_value[0])
                except (KeyError, IndexError, TypeError):
                    pass
    return 1.0


def _material_tile_offset(mat):
    """Resolve per-material tile_offset (Mapping node Location.x/y)."""
    if mat is None:
        return (0.0, 0.0)
    if "soob_tile_offset" in mat:
        try:
            v = mat["soob_tile_offset"]
            return (float(v[0]), float(v[1]))
        except (TypeError, ValueError, IndexError):
            pass
    if mat.use_nodes and mat.node_tree is not None:
        for node in mat.node_tree.nodes:
            if node.type == "MAPPING":
                try:
                    loc = node.inputs["Location"].default_value
                    return (float(loc[0]), float(loc[1]))
                except (KeyError, IndexError, TypeError):
                    pass
    return (0.0, 0.0)


def _material_diffuse_filename(mat):
    """Find the diffuse PNG filename for a material's MTL `map_Kd`.

    Walks the shader node tree for the first Image Texture node feeding
    Base Color. Returns just the basename — the engine resolves it
    against assets/textures/ (or wherever assets.lua points). Returns
    None if no usable texture is found.
    """
    if mat is None or not mat.use_nodes or mat.node_tree is None:
        return None
    for node in mat.node_tree.nodes:
        if node.type == "TEX_IMAGE" and node.image is not None:
            img = node.image
            if img.filepath:
                return os.path.basename(bpy.path.abspath(img.filepath))
            return img.name  # packed image — name as-is
    return None


def _material_lightmap_filename(mat):
    """MTL `# lm_map` filename. Default: <material>_lm.png."""
    if mat is None:
        return None
    if "soob_lm_map" in mat:
        return str(mat["soob_lm_map"])
    return "%s_lm.png" % mat.name


def _material_alpha_test(mat):
    """Return alpha threshold (0..1) or None if alpha-test is off."""
    if mat is None:
        return None
    if "soob_alpha_test" in mat:
        try:
            return float(mat["soob_alpha_test"])
        except (TypeError, ValueError):
            return None
    return None


def _write_mtl(materials, mtl_path):
    """Write the .mtl file. materials = list of bpy.types.Material (no Nones)."""
    with open(mtl_path, "w", encoding="utf-8") as f:
        f.write("# SOOB Engine material library\n")
        for mat in materials:
            f.write("\nnewmtl %s\n" % mat.name)
            diffuse = _material_diffuse_filename(mat)
            if diffuse:
                f.write("map_Kd %s\n" % diffuse)
            lm = _material_lightmap_filename(mat)
            if lm:
                f.write("# lm_map %s\n" % lm)
            scale = _material_tile_scale(mat)
            if scale != 1.0:
                f.write("# tile_scale %.4f\n" % scale)
            ox, oy = _material_tile_offset(mat)
            if ox != 0.0 or oy != 0.0:
                f.write("# tile_offset %.4f %.4f\n" % (ox, oy))
            at = _material_alpha_test(mat)
            if at is not None:
                f.write("# alpha_test %.3f\n" % at)


def _write_obj(obj, depsgraph, obj_path, mtl_basename, lightmap_uv_name):
    """Write the .obj file for one Blender object.

    Returns (vert_count, face_count, material_list_used). The material
    list is the subset of obj.data.materials that actually has faces
    pointing at it, in the order they first appear — used by the caller
    to write a matching .mtl.

    `vt` and `vn` are deduplicated by rounded value (matching the float
    precision actually written) so axis-aligned level geometry produces
    output comparable in size to Blender's built-in exporter — ~8 unique
    normals on a boxy room rather than one per loop. Faces are emitted
    as-is (no pre-triangulation); the engine parser fan-triangulates on
    load (obj_loader.h:296).
    """
    eval_obj = obj.evaluated_get(depsgraph)
    mesh = eval_obj.to_mesh()
    try:
        # World-space transform (level OBJ has no object transform).
        mesh.transform(eval_obj.matrix_world)
        try:
            mesh.calc_normals_split()
        except AttributeError:
            # Blender 4.1+ removed calc_normals_split (auto split normals).
            # mesh.loops[i].normal is already populated by Blender there.
            pass

        uv_layer = _lightmap_uv_layer(mesh, lightmap_uv_name)

        # ---- Dedup tables for vt and vn ----------------------------------
        #
        # Key is the rounded float tuple written verbatim into the file,
        # so two values that would print identically share an index. This
        # makes the output stable across reorderings and matches what
        # Blender's built-in OBJ exporter does for boxy levels.

        VN_FMT = "%.4f %.4f %.4f"
        VT_FMT = "%.6f %.6f"

        vn_index = {}     # key -> 1-based index
        vn_list  = []     # ordered (x, y, z) tuples, ready to print

        def vn_lookup(normal):
            nx, ny, nz = _swap_axes(normal)
            key = VN_FMT % (nx, ny, nz)
            idx = vn_index.get(key)
            if idx is None:
                vn_list.append((nx, ny, nz))
                idx = len(vn_list)
                vn_index[key] = idx
            return idx

        vt_index = {}
        vt_list  = []

        def vt_lookup(uv):
            u, v = uv[0], uv[1]
            key = VT_FMT % (u, v)
            idx = vt_index.get(key)
            if idx is None:
                vt_list.append((u, v))
                idx = len(vt_list)
                vt_index[key] = idx
            return idx

        # ---- First pass: walk polygons, fill dedup tables, also record
        #      which materials are actually used (in first-seen order).
        used_mat_indices = []
        per_poly_refs = []  # parallel list of [(v_idx_1based, vt_idx_or_0, vn_idx), ...]

        polys = sorted(mesh.polygons, key=lambda p: p.material_index)
        for poly in polys:
            mi = poly.material_index
            if mi not in used_mat_indices:
                used_mat_indices.append(mi)
            refs = []
            for li in poly.loop_indices:
                loop = mesh.loops[li]
                v_idx = loop.vertex_index + 1
                vn_idx = vn_lookup(loop.normal)
                if uv_layer is not None:
                    vt_idx = vt_lookup(uv_layer.data[li].uv)
                else:
                    vt_idx = 0
                refs.append((v_idx, vt_idx, vn_idx))
            per_poly_refs.append((mi, refs))

        used_materials = []
        for mi in used_mat_indices:
            slot = obj.data.materials[mi] if mi < len(obj.data.materials) else None
            if slot is not None:
                used_materials.append(slot)

        # ---- Second pass: emit the file. -----------------------------------
        with open(obj_path, "w", encoding="utf-8") as f:
            f.write("# SOOB Engine level export\n")
            f.write("# Source: %s\n" % obj.name)
            if used_materials:
                f.write("mtllib %s\n" % mtl_basename)
            f.write("o %s\n" % obj.name)

            for vert in mesh.vertices:
                x, y, z = _swap_axes(vert.co)
                f.write("v %.6f %.6f %.6f\n" % (x, y, z))

            for u, v in vt_list:
                f.write("vt " + (VT_FMT % (u, v)) + "\n")

            for n in vn_list:
                f.write("vn " + (VN_FMT % n) + "\n")

            current_mat = -2  # sentinel different from any real index
            for mi, refs in per_poly_refs:
                if mi != current_mat:
                    mat = obj.data.materials[mi] if mi < len(obj.data.materials) else None
                    if mat is not None:
                        f.write("usemtl %s\n" % mat.name)
                    current_mat = mi

                parts = []
                for v_idx, vt_idx, vn_idx in refs:
                    if uv_layer is not None:
                        parts.append("%d/%d/%d" % (v_idx, vt_idx, vn_idx))
                    else:
                        parts.append("%d//%d" % (v_idx, vn_idx))
                f.write("f " + " ".join(parts) + "\n")

        return (len(mesh.vertices), len(mesh.polygons), used_materials)
    finally:
        eval_obj.to_mesh_clear()


# ---------------------------------------------------------------------------
# Lightmap bake
# ---------------------------------------------------------------------------
#
# Per-material Cycles bake. Each material on the level mesh gets its own
# baked image saved as <material>_lm.png next to the OBJ. Inside Blender
# we mark a per-material Image Texture node (named `_SOOB_LM`) as the
# active node so Cycles writes into the right image per face — one bake
# call handles all materials simultaneously.
#
# The bake node is created on first use and left in place so subsequent
# bakes don't have to re-create it. It's tagged by name (`_SOOB_LM`) and
# kept disconnected so it doesn't affect the viewport material preview.

SOOB_BAKE_NODE_NAME = "_SOOB_LM"


def _ensure_lightmap_uv(obj, uv_name, auto_create, margin):
    """Make sure obj has a UV map named `uv_name`.

    If missing and auto_create is True, runs Smart UV Project on a fresh
    UV layer. Returns the layer object (or None if the mesh is unsuitable).
    The created layer is made active so the bake writes into it.
    """
    mesh = obj.data
    if not hasattr(mesh, "uv_layers"):
        return None
    layer = mesh.uv_layers.get(uv_name)
    if layer is not None:
        mesh.uv_layers.active = layer
        return layer
    if not auto_create:
        return None

    # Add the layer + make it active before unwrapping, so Smart UV
    # Project writes into the new one rather than overwriting whatever
    # was already there.
    layer = mesh.uv_layers.new(name=uv_name, do_init=False)
    if layer is None:
        return None
    mesh.uv_layers.active = layer

    # Smart UV Project is an Edit-Mode operator. We toggle in, run it,
    # toggle back. Selection state is preserved by the operator's own
    # "select all" before we call it (so partial selections don't bite).
    prev_mode = obj.mode
    if prev_mode != "EDIT":
        bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(
        angle_limit=1.15192,  # ~66 degrees, the level-design.md default
        island_margin=margin,
        correct_aspect=True,
        scale_to_bounds=False,
    )
    if prev_mode != "EDIT":
        bpy.ops.object.mode_set(mode=prev_mode)
    return layer


def _find_or_create_bake_node(mat):
    """Return the `_SOOB_LM` Image Texture node in mat's tree, creating
    one if absent. Caller binds an image to .image and makes it active.
    """
    if not mat.use_nodes:
        mat.use_nodes = True
    tree = mat.node_tree
    for n in tree.nodes:
        if n.name == SOOB_BAKE_NODE_NAME or n.label == SOOB_BAKE_NODE_NAME:
            return n
    node = tree.nodes.new(type="ShaderNodeTexImage")
    node.name = SOOB_BAKE_NODE_NAME
    node.label = SOOB_BAKE_NODE_NAME
    # Park it off to the side so it doesn't overlap the user's nodes.
    node.location = (-600.0, -400.0)
    return node


def _get_or_create_lightmap_image(mat_name, resolution):
    """Return the bpy image for this material's lightmap, creating one
    at the requested resolution if needed. Uses sRGB color space since
    the engine reads PNGs as gamma-corrected bytes.
    """
    img_name = "%s_lm" % mat_name
    img = bpy.data.images.get(img_name)
    if img is not None:
        if img.size[0] != resolution or img.size[1] != resolution:
            # Recreate at new resolution.
            bpy.data.images.remove(img)
            img = None
    if img is None:
        img = bpy.data.images.new(
            img_name, width=resolution, height=resolution,
            alpha=False, float_buffer=False,
        )
        img.colorspace_settings.name = "sRGB"
    return img


def _bake_lightmaps(obj, export_dir, resolution, samples, margin, uv_name,
                    auto_uv, uv_island_margin):
    """Run the per-material Cycles bake for `obj`.

    Steps:
      1. Ensure the Lightmap UV map exists (auto-create if requested).
      2. For each material slot, bind a per-material Image Texture node
         to a fresh sRGB image at `resolution`, mark it as the active
         texture node so Cycles bakes into it.
      3. Stash + flip render engine/samples, run the bake operator.
      4. Save each baked image as <material>_lm.png in export_dir.
      5. Restore render engine and samples.

    Returns a list of (material_name, output_path) for each image saved.
    Logs to the console; raises on hard failures (bad object, no
    materials, bake operator returned non-FINISHED).
    """
    if obj is None or obj.type != "MESH":
        raise RuntimeError("Bake target is not a mesh")
    if not obj.data.materials:
        print("[soob] bake skipped: no materials on %s" % obj.name)
        return []

    layer = _ensure_lightmap_uv(obj, uv_name, auto_uv, uv_island_margin)
    if layer is None:
        raise RuntimeError(
            "No Lightmap UV map on %s (enable Auto-create lightmap UVs "
            "or add one named %r)" % (obj.name, uv_name)
        )

    # Bind one image per material slot, remember the previously active
    # node so we can restore it. Skip empty slots and skip duplicate
    # references — two slots can legally point at the same material.
    binds = []           # (material, image, previous_active_node)
    seen_materials = set()
    for slot in obj.data.materials:
        if slot is None or slot.name in seen_materials:
            continue
        seen_materials.add(slot.name)
        img = _get_or_create_lightmap_image(slot.name, resolution)
        node = _find_or_create_bake_node(slot)
        node.image = img
        prev_active = slot.node_tree.nodes.active
        slot.node_tree.nodes.active = node
        node.select = True
        binds.append((slot, img, prev_active))

    if not binds:
        print("[soob] bake skipped: no usable material slots on %s" % obj.name)
        return []

    scene = bpy.context.scene
    prev_engine = scene.render.engine
    prev_samples = None
    try:
        scene.render.engine = "CYCLES"
        prev_samples = scene.cycles.samples
        scene.cycles.samples = samples

        # Bake operator works on the active selected object.
        view_layer = bpy.context.view_layer
        prev_active_obj = view_layer.objects.active
        prev_selected = [o for o in bpy.context.selected_objects]
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        view_layer.objects.active = obj

        # Diffuse > Direct + Indirect: lighting only, no surface color
        # (matches `docs/level-design.md` "Bake Lightmap" recipe).
        result = bpy.ops.object.bake(
            type="DIFFUSE",
            pass_filter={"DIRECT", "INDIRECT"},
            use_clear=True,
            margin=max(2, int(margin)),
            uv_layer=uv_name,
        )
        if "FINISHED" not in result:
            raise RuntimeError("Cycles bake operator returned %s" % result)

        # Save baked images.
        saved = []
        for mat, img, _prev in binds:
            out_path = os.path.join(export_dir, "%s_lm.png" % mat.name)
            img.filepath_raw = out_path
            img.file_format = "PNG"
            img.save_render(out_path)
            saved.append((mat.name, out_path))
            print("[soob] baked lightmap: %s" % out_path)

        # Restore selection state.
        bpy.ops.object.select_all(action="DESELECT")
        for o in prev_selected:
            try:
                o.select_set(True)
            except RuntimeError:
                pass
        if prev_active_obj is not None:
            view_layer.objects.active = prev_active_obj

        return saved
    finally:
        # Restore active texture nodes — leave the _SOOB_LM nodes in
        # place though, so the next bake doesn't have to re-create them.
        for mat, _img, prev_active in binds:
            if prev_active is not None:
                mat.node_tree.nodes.active = prev_active
        scene.render.engine = prev_engine
        if prev_samples is not None:
            scene.cycles.samples = prev_samples


# ---------------------------------------------------------------------------
# Asset copy
# ---------------------------------------------------------------------------
#
# Optional post-export pass that resolves every entity's mesh/tex/iqm
# reference against assets.lua and copies the source file to the export
# root. Existing files at the destination are left alone (no clobber)
# so registered assets stay intact and a re-export doesn't repeatedly
# overwrite the user's textures.

_ASSETS_LUA_NAME = "assets.lua"


def _find_assets_lua(start_dir):
    """Walk up from start_dir looking for assets.lua. Returns the absolute
    path or None."""
    cur = os.path.abspath(start_dir)
    for _ in range(6):
        candidate = os.path.join(cur, _ASSETS_LUA_NAME)
        if os.path.isfile(candidate):
            return candidate
        parent = os.path.dirname(cur)
        if parent == cur:
            break
        cur = parent
    return None


def _parse_assets_lua(path):
    """Minimal Lua parser tailored for assets.lua.

    Recognises top-level table sections (`models = {`, `textures = {`, ...)
    and `name = "string"` entries inside them. Doesn't try to support the
    full Lua grammar — anything fancier in assets.lua will simply be
    ignored. Returns {section: {logical_name: path}}.
    """
    import re
    section_re = re.compile(r'^\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*\{')
    entry_re   = re.compile(
        r'^\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*"([^"]+)"\s*,?'
    )

    sections = {}
    current_section = None
    depth = 0
    try:
        with open(path, "r", encoding="utf-8") as f:
            lines = f.readlines()
    except OSError:
        return sections

    for raw in lines:
        # Strip Lua line comments.
        line = raw.split("--", 1)[0]
        # Track brace depth so nested tables don't confuse the section
        # boundary heuristic.
        opens  = line.count("{")
        closes = line.count("}")

        if current_section is None or depth == 1:
            m = section_re.search(line)
            if m and depth + opens >= 1:
                current_section = m.group(1)
                sections.setdefault(current_section, {})

        if current_section is not None and depth >= 1:
            m = entry_re.search(line)
            if m:
                sections[current_section][m.group(1)] = m.group(2)

        depth += opens - closes
        if depth <= 1:
            # Left this section's table — wait for the next section header.
            if depth < 1:
                depth = 0
                current_section = None

    return sections


def _resolve_asset(section, logical_or_path):
    """Look up a logical name in the assets table; falls through to a raw
    path if the name isn't registered.
    """
    if not logical_or_path:
        return None
    if logical_or_path in section:
        return section[logical_or_path]
    return logical_or_path  # treat as raw path


def _copy_asset(src_root, dst_root, rel_path, copied):
    """Copy src_root/rel_path -> dst_root/rel_path if missing. Records
    the action in `copied` for the toast log.
    """
    import shutil
    src = os.path.normpath(os.path.join(src_root, rel_path))
    dst = os.path.normpath(os.path.join(dst_root, rel_path))
    if not os.path.isfile(src):
        print("[soob] asset copy: source missing, skipped: %s" % src)
        return
    if os.path.isfile(dst):
        print("[soob] asset copy: destination exists, skipped: %s" % rel_path)
        return
    try:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
    except OSError:
        pass
    shutil.copy2(src, dst)
    copied.append(rel_path)
    print("[soob] asset copy: %s" % rel_path)


def _copy_referenced_assets(scene_objects, src_root, dst_root):
    """Walk every entity, resolve mesh/tex/iqm via assets.lua, copy each
    missing file into the export tree. Returns list of relative paths
    copied (for the toast log).
    """
    assets_path = _find_assets_lua(src_root)
    if assets_path is None:
        print("[soob] asset copy skipped: no assets.lua found near %s" % src_root)
        return []

    parsed = _parse_assets_lua(assets_path)
    models   = parsed.get("models",   {})
    textures = parsed.get("textures", {})

    repo_root = os.path.dirname(assets_path)
    copied = []
    seen = set()  # don't try to copy the same file twice in one export

    for obj in scene_objects:
        if obj.soob.type == "none":
            continue
        common = obj.soob.common
        for value, table in (
            (common.mesh, models),
            (common.iqm,  models),
            (common.tex,  textures),
        ):
            if not value:
                continue
            resolved = _resolve_asset(table, value)
            if not resolved or resolved in seen:
                continue
            seen.add(resolved)
            _copy_asset(repo_root, dst_root, resolved, copied)

    return copied


# ---------------------------------------------------------------------------
# Viewport overlays
# ---------------------------------------------------------------------------
#
# GPU draw handler that renders entity gizmos in world space:
#   - path-group polylines (cyan) sorted by path_node.order
#   - magenta forward arrow on each platform leader, pointing at its
#     first path node (mirrors pathDebugRender in the engine)
#   - yellow translucent box around each trigger, sized by obj.scale * 2
#   - small per-entity icon markers (typed colored octahedrons)
#
# Toggled via a BoolProperty on the Scene; the N-panel in the 3D View
# exposes the checkbox. The handler is registered/unregistered with the
# addon, and the toggle just gates the actual draw work.

_DRAW_HANDLER = [None]  # mutable cell so register() can stash + clear it


def _gpu_imports():
    """Lazy-import gpu + gpu_extras so the addon loads even when blender
    is running headless without a gpu module (rare, but cleaner failure).
    """
    import gpu
    from gpu_extras.batch import batch_for_shader
    return gpu, batch_for_shader


def _overlay_collect_paths(scene):
    """Return {group_name: [(world_pos, order, obj), ...]} grouped by
    path_node.group and sorted by order within each group.
    """
    groups = {}
    for obj in scene.objects:
        if obj.soob.type != "path_node":
            continue
        group = obj.soob.common.group or "-"
        groups.setdefault(group, []).append(
            (obj.matrix_world.to_translation(),
             obj.soob.path_node.order, obj)
        )
    for group in groups:
        groups[group].sort(key=lambda t: (t[1], t[2].name))
    return groups


def _overlay_collect_triggers(scene):
    """Return [(center_world, half_extents)] for each trigger entity."""
    out = []
    for obj in scene.objects:
        if obj.soob.type != "trigger":
            continue
        center = obj.matrix_world.to_translation()
        sx = abs(obj.scale[0])
        sy = abs(obj.scale[1])
        sz = abs(obj.scale[2])
        out.append((center, (sx, sy, sz)))
    return out


def _overlay_box_lines(center, half):
    """Generate 24 line endpoints (12 edges) for an axis-aligned box."""
    cx, cy, cz = center[0], center[1], center[2]
    hx, hy, hz = half
    # 8 corners
    c = [
        (cx-hx, cy-hy, cz-hz), (cx+hx, cy-hy, cz-hz),
        (cx+hx, cy+hy, cz-hz), (cx-hx, cy+hy, cz-hz),
        (cx-hx, cy-hy, cz+hz), (cx+hx, cy-hy, cz+hz),
        (cx+hx, cy+hy, cz+hz), (cx-hx, cy+hy, cz+hz),
    ]
    edges = [
        (0,1),(1,2),(2,3),(3,0),     # bottom
        (4,5),(5,6),(6,7),(7,4),     # top
        (0,4),(1,5),(2,6),(3,7),     # verticals
    ]
    lines = []
    for a, b in edges:
        lines.append(c[a])
        lines.append(c[b])
    return lines


def _overlay_collect_platform_leaders(scene, path_groups):
    """For each platform whose `path` field references a known group,
    return (platform_world_pos, first_node_world_pos) for the leader
    arrow. Multiple platforms can share a group; only the first one
    seen wins (matches the engine's leader-picking convention).
    """
    seen_groups = set()
    out = []
    for obj in scene.objects:
        if obj.soob.type != "platform":
            continue
        group = obj.soob.platform.path
        if not group or group not in path_groups or group in seen_groups:
            continue
        nodes = path_groups[group]
        if not nodes:
            continue
        seen_groups.add(group)
        out.append((obj.matrix_world.to_translation(), nodes[0][0]))
    return out


def _overlay_draw():
    """Draw handler — runs every redraw of every 3D View.

    Cheap on empty scenes; on a level with ~256 entities the batched
    geometry is a few hundred vertices total, well below anything that
    would impact viewport FPS.
    """
    context = bpy.context
    scene = context.scene
    if not getattr(scene, "soob_show_overlays", False):
        return

    try:
        gpu, batch_for_shader = _gpu_imports()
    except ImportError:
        return  # gpu module unavailable; silently no-op

    shader = gpu.shader.from_builtin("UNIFORM_COLOR")
    gpu.state.line_width_set(2.0)
    gpu.state.blend_set("ALPHA")

    # ---- Path polylines + leader arrows ------------------------------
    path_groups = _overlay_collect_paths(scene)
    line_pts = []
    for group, nodes in path_groups.items():
        for i in range(len(nodes) - 1):
            line_pts.append(nodes[i][0])
            line_pts.append(nodes[i + 1][0])
    if line_pts:
        batch = batch_for_shader(shader, "LINES", {"pos": line_pts})
        shader.bind()
        shader.uniform_float("color", (0.0, 0.85, 1.0, 1.0))  # cyan
        batch.draw(shader)

    leader_arrows = _overlay_collect_platform_leaders(scene, path_groups)
    arrow_pts = []
    for src, dst in leader_arrows:
        arrow_pts.append(src)
        arrow_pts.append(dst)
    if arrow_pts:
        gpu.state.line_width_set(3.0)
        batch = batch_for_shader(shader, "LINES", {"pos": arrow_pts})
        shader.bind()
        shader.uniform_float("color", (1.0, 0.0, 0.85, 1.0))  # magenta
        batch.draw(shader)
        gpu.state.line_width_set(2.0)

    # ---- Trigger boxes (yellow wireframe, ~50% alpha) -----------------
    triggers = _overlay_collect_triggers(scene)
    tri_lines = []
    for center, half in triggers:
        tri_lines.extend(_overlay_box_lines(center, half))
    if tri_lines:
        batch = batch_for_shader(shader, "LINES", {"pos": tri_lines})
        shader.bind()
        shader.uniform_float("color", (1.0, 0.9, 0.0, 0.6))  # yellow
        batch.draw(shader)

    # ---- Path-node markers (small magenta octahedra) ------------------
    node_lines = []
    for group, nodes in path_groups.items():
        for pos, _order, _obj in nodes:
            r = 0.15
            cx, cy, cz = pos[0], pos[1], pos[2]
            # 6-vertex octahedron edges (12 edges, 24 points)
            tips = [
                (cx+r, cy,   cz),   (cx-r, cy,   cz),
                (cx,   cy+r, cz),   (cx,   cy-r, cz),
                (cx,   cy,   cz+r), (cx,   cy,   cz-r),
            ]
            pairs = [
                (0,2),(0,3),(0,4),(0,5),
                (1,2),(1,3),(1,4),(1,5),
                (2,4),(2,5),(3,4),(3,5),
            ]
            for a, b in pairs:
                node_lines.append(tips[a])
                node_lines.append(tips[b])
    if node_lines:
        batch = batch_for_shader(shader, "LINES", {"pos": node_lines})
        shader.bind()
        shader.uniform_float("color", (1.0, 0.2, 1.0, 1.0))  # magenta
        batch.draw(shader)

    # Restore reasonable defaults so we don't bleed state into other
    # draw callbacks running after us.
    gpu.state.line_width_set(1.0)
    gpu.state.blend_set("NONE")


class SOOB_PT_view3d_panel(Panel):
    """N-panel sidebar in the 3D View with overlay + export shortcuts."""

    bl_label       = "SOOB"
    bl_idname      = "SOOB_PT_view3d_panel"
    bl_space_type  = "VIEW_3D"
    bl_region_type = "UI"
    bl_category    = "SOOB"

    def draw(self, context):
        layout = self.layout
        scene = context.scene
        layout.prop(scene, "soob_show_overlays", text="Show Entity Overlays",
                    toggle=True)
        layout.separator()
        layout.operator("export_scene.soob_level", text="Export Level",
                        icon="EXPORT")


# ---------------------------------------------------------------------------
# .ent writer
# ---------------------------------------------------------------------------
#
# One writer per entity type, mirroring entity.h:entLoadFile so a round-
# tripped file diffs cleanly. Format per the parser:
#
#     <type> <name> <group> <posX> <posY> <posZ> <rotY> [key=value ...]
#
# Defaults that match the engine's parser defaults are deliberately
# omitted to keep the output tidy. Float formatter trims trailing zeros
# so 1.500 -> 1.5, 0.000 -> 0.

def _f(value):
    """Compact float -> string. Trims trailing zeros after the decimal."""
    s = "%.3f" % float(value)
    if "." in s:
        s = s.rstrip("0").rstrip(".")
    return s or "0"


def _ent_pos_and_yaw(obj):
    """Blender world transform -> engine (posX, posY, posZ, rotY).

    Same axis swap as the OBJ writer: X=x, Y=z, Z=-y. Yaw comes from
    Blender's Z-axis rotation (mapped to engine Y-axis), in degrees.
    Non-yaw Eulers are deliberately discarded — entities are gravity-
    aligned in the engine.
    """
    loc = obj.matrix_world.to_translation()
    rot_eul = obj.matrix_world.to_euler("XYZ")
    import math
    pos = (loc[0], loc[2], -loc[1])
    yaw_deg = math.degrees(rot_eul.z)
    return pos[0], pos[1], pos[2], yaw_deg


def _ent_entity_name(obj):
    common = obj.soob.common
    if common.name_override:
        return common.name_override
    return obj.name


def _ent_entity_group(obj):
    g = obj.soob.common.group
    return g if g else "-"


def _ent_common_keys(obj):
    """Return list of "key=value" tokens for the shared (common) fields.

    Only emitted when the field's value differs from the engine's parser
    default. mesh/tex/iqm pass through verbatim (logical names from
    assets.lua, or raw paths).
    """
    common = obj.soob.common
    keys = []
    if common.mesh:       keys.append("mesh=" + common.mesh)
    if common.tex:        keys.append("tex=" + common.tex)
    if common.iqm:        keys.append("iqm=" + common.iqm)
    if common.anim:       keys.append("anim=" + common.anim)
    if common.scale != 1.0:
        keys.append("scale=" + _f(common.scale))
    if common.static:     keys.append("static=1")
    if common.flip_cull:  keys.append("flip_cull=1")
    if common.anim_speed != 1.0:
        keys.append("anim_speed=" + _f(common.anim_speed))
    if common.collide == "box":
        keys.append("collide=box")
    elif common.collide == "trimesh":
        keys.append("collide=trimesh")
    return keys


def _write_ent_line(obj, file):
    """Emit one .ent line for `obj`. Returns the entity type token, or
    None if this object isn't an exportable entity (type == 'none').
    """
    soob_type = obj.soob.type
    if soob_type == "none":
        return None

    name  = _ent_entity_name(obj)
    group = _ent_entity_group(obj)
    px, py, pz, ry = _ent_pos_and_yaw(obj)
    common_keys = _ent_common_keys(obj)

    # Type-specific keys. We use the same "only emit non-default" rule.
    type_keys = []
    if soob_type == "item":
        if obj.soob.item.item_type != "0":
            type_keys.append("item_type=" + obj.soob.item.item_type)
    elif soob_type == "enemy":
        e = obj.soob.enemy
        if e.health != 100: type_keys.append("health=" + str(e.health))
        if e.speed  != 3.0: type_keys.append("speed="  + _f(e.speed))
        if e.sight  != 15.0: type_keys.append("sight=" + _f(e.sight))
    elif soob_type == "platform":
        p = obj.soob.platform
        if p.path:                 type_keys.append("path=" + p.path)
        if p.move != "once":       type_keys.append("move=" + p.move)
        if p.speed != 1.5:         type_keys.append("speed=" + _f(p.speed))
        if not p.enabled:          type_keys.append("enabled=0")
        if p.face_path:            type_keys.append("face_path=1")
    elif soob_type == "switch":
        if obj.soob.switch.target:
            type_keys.append("target=" + obj.soob.switch.target)
    elif soob_type == "trigger":
        t = obj.soob.trigger
        # Engine stores `size=` as HALF-extents (entity.h:465 checks
        # `dx > -sx && dx < sx`). The viewport overlay draws a box
        # of total width 2*scale, so Blender scale=1 displays as a 2m
        # cube and the .ent gets size=1,1,1 — same visual, same volume.
        sx = abs(obj.scale[0])
        sy = abs(obj.scale[2])  # Blender Z = engine Y
        sz = abs(obj.scale[1])
        if sx != 0.0 or sy != 0.0 or sz != 0.0:
            type_keys.append("size=%s,%s,%s" % (_f(sx), _f(sy), _f(sz)))
        if t.target: type_keys.append("target=" + t.target)
        if t.once:   type_keys.append("once=1")
    elif soob_type == "door":
        d = obj.soob.door
        if d.motion != "slide": type_keys.append("motion=" + d.motion)
        if d.axis != "Y":       type_keys.append("axis=" + d.axis)
        if d.amount != (90.0 if d.motion == "rotate" else 1.0):
            type_keys.append("amount=" + _f(d.amount))
        if d.speed != 1.0:      type_keys.append("speed=" + _f(d.speed))
        if d.auto_close != 0.0: type_keys.append("auto_close=" + _f(d.auto_close))
    elif soob_type == "path_node":
        if obj.soob.path_node.order != 0:
            type_keys.append("order=" + str(obj.soob.path_node.order))
    # player / waypoint: position-only, no type-specific keys.

    keys = common_keys + type_keys
    f = file
    f.write("%s %s %s %s %s %s %s" % (
        soob_type, name, group, _f(px), _f(py), _f(pz), _f(ry)
    ))
    if keys:
        f.write(" " + " ".join(keys))
    f.write("\n")
    return soob_type


def _write_ent_file(scene_objects, ent_path):
    """Write the .ent file. Returns counts per type for the toast log."""
    # Order: write path_nodes first (so platforms reference an already-
    # known group), then platforms, then everything else by name. This
    # matches what `entLoadFile` and `pathTableBuild` expect.
    type_sort = {
        "player": 0, "path_node": 1, "waypoint": 2, "platform": 3,
        "door": 4, "switch": 5, "trigger": 6,
        "enemy": 7, "item": 8, "decoration": 9,
    }

    entities = [o for o in scene_objects if o.soob.type != "none"]
    entities.sort(key=lambda o: (type_sort.get(o.soob.type, 99), o.name))

    counts = {}
    with open(ent_path, "w", encoding="utf-8") as f:
        f.write("# Entity definitions, exported by SOOB Engine Blender addon.\n")
        f.write("# Format: type name group posX posY posZ rotY [key=value ...]\n")
        f.write("\n")
        current_type = None
        for obj in entities:
            t = obj.soob.type
            if t != current_type:
                f.write("# %s entities\n" % t)
                current_type = t
            written = _write_ent_line(obj, f)
            if written is not None:
                counts[written] = counts.get(written, 0) + 1
    return counts


# ---------------------------------------------------------------------------
# Property groups
# ---------------------------------------------------------------------------
#
# Every Blender object gets a `soob` PointerProperty whose `type` field
# decides how the exporter classifies it:
#
#     obj.soob.type == "none"   -> level geometry (default; no property panel
#                                  fields beyond the dropdown itself)
#     obj.soob.type == "..."    -> entity of that type; type-specific
#                                  subpanels show in later steps
#
# Keep the enum identifiers identical to the .ent file's type tokens
# (player / decoration / item / ...) so the .ent writer can use them
# verbatim. The "none" identifier is exporter-internal — never written.

SOOB_TYPE_ITEMS = (
    ("none",       "None (Level Geometry)",
        "Default. Object is joined into the level OBJ and lightmap bake"),
    ("player",     "Player Spawn",
        "Spawn position and yaw for the player"),
    ("decoration", "Decoration",
        "Static or animated prop; mesh OBJ or IQM"),
    ("item",       "Item",
        "Pickup (health / ammo / key)"),
    ("enemy",      "Enemy",
        "Spawn point for an enemy (IQM character)"),
    ("platform",   "Platform",
        "Path-following moving platform (see docs/PLAN_PATH_PLATFORMS.md)"),
    ("switch",     "Switch",
        "Activatable object that targets another entity"),
    ("trigger",    "Trigger",
        "Invisible volume that fires on player overlap"),
    ("door",       "Door",
        "Sliding or rotating door"),
    ("waypoint",   "Waypoint",
        "Nav-graph node for AI pathing"),
    ("path_node",  "Path Node",
        "Ordered waypoint along a platform's path"),
)


# Nested property groups — one per entity type with type-specific fields.
# Field names mirror the .ent key=value tokens in entity.h:entLoadFile
# (search `else if (strcmp(key,`) so the writer in Step 6 can map them
# 1:1. Defaults match the engine defaults so the writer can skip emitting
# keys that haven't been touched.

class SOOB_PG_common(PropertyGroup):
    """Fields shared by every entity (name/group/collider/scale/visual binding)."""

    # `name`/`group` override the derived defaults (obj.name and "-").
    name_override: StringProperty(
        name="Name",
        description="Override the entity name. Blank = use Blender object name",
        default="",
    )
    group: StringProperty(
        name="Group",
        description="Optional group for batch activation. Blank = '-'",
        default="",
    )
    collide: EnumProperty(
        name="Collide",
        items=(
            ("none",    "None",    "No physics body"),
            ("box",     "Box AABB", "Axis-aligned bounding box from mesh"),
            ("trimesh", "Trimesh", "Exact triangle mesh (slower; use for kneeholes)"),
        ),
        default="none",
    )
    scale: FloatProperty(
        name="Scale",
        description="Uniform scale factor applied to the entity's mesh",
        default=1.0, min=0.001, soft_max=10.0,
    )
    static: BoolProperty(
        name="Static",
        description="Include this entity's mesh in the lightmap bake",
        default=False,
    )
    flip_cull: BoolProperty(
        name="Flip Cull",
        description="Use GL_FRONT culling (for inside-out meshes)",
        default=False,
    )

    # Visual binding — logical names from assets.lua (mesh=, tex=, iqm=).
    # Unknown names fall through as raw paths.
    mesh: StringProperty(
        name="Mesh",
        description="assets.lua logical name (e.g. 'office_desk') or path. "
                    "Use for static OBJ props",
        default="",
    )
    tex: StringProperty(
        name="Texture",
        description="assets.lua logical name (e.g. 'wood1') or path",
        default="",
    )
    iqm: StringProperty(
        name="IQM",
        description="assets.lua logical name for an animated IQM model. "
                    "Mutually exclusive with Mesh",
        default="",
    )
    anim: StringProperty(
        name="Initial Anim",
        description="IQM animation name to start playing on load",
        default="",
    )
    anim_speed: FloatProperty(
        name="Anim Speed",
        description="Animation playback rate multiplier",
        default=1.0, min=0.0, soft_max=4.0,
    )


class SOOB_PG_item(PropertyGroup):
    item_type: EnumProperty(
        name="Item Type",
        items=(
            ("0", "Health", ""),
            ("1", "Ammo",   ""),
            ("2", "Key",    ""),
        ),
        default="0",
    )


class SOOB_PG_enemy(PropertyGroup):
    health: IntProperty(name="Health", default=100, min=1, soft_max=1000)
    speed: FloatProperty(name="Speed",  default=3.0, min=0.0, soft_max=20.0)
    sight: FloatProperty(name="Sight Range", default=15.0, min=0.0, soft_max=100.0)


class SOOB_PG_platform(PropertyGroup):
    path: StringProperty(
        name="Path Group",
        description="Name of the path_node group this platform follows",
        default="",
    )
    move: EnumProperty(
        name="Movement",
        items=(
            ("once",      "Once",      "Travel from node 0 to last, then stop"),
            ("ping_pong", "Ping-pong", "Bounce back and forth between endpoints"),
        ),
        default="once",
    )
    speed: FloatProperty(name="Speed", default=1.5, min=0.0, soft_max=20.0)
    enabled: BoolProperty(
        name="Enabled",
        description="Whether motion is running on load (off = needs a trigger)",
        default=True,
    )
    face_path: BoolProperty(
        name="Face Path",
        description="Yaw the platform to align with the current segment heading",
        default=False,
    )


class SOOB_PG_switch(PropertyGroup):
    target: StringProperty(name="Target", default="")


class SOOB_PG_trigger(PropertyGroup):
    # Trigger size comes from obj.scale * 2 (1 = 2m box) — no field here.
    target: StringProperty(name="Target", default="")
    once: BoolProperty(name="Once", default=False)


class SOOB_PG_door(PropertyGroup):
    motion: EnumProperty(
        name="Motion",
        items=(
            ("slide",  "Slide",  "Translate along axis"),
            ("rotate", "Rotate", "Rotate around Y axis"),
        ),
        default="slide",
    )
    axis: EnumProperty(
        name="Axis",
        items=(("X", "X", ""), ("Y", "Y", ""), ("Z", "Z", "")),
        default="Y",
    )
    amount: FloatProperty(
        name="Amount",
        description="Meters (slide) or degrees (rotate)",
        default=1.0,
    )
    speed: FloatProperty(name="Speed", default=1.0, min=0.0, soft_max=20.0)
    auto_close: FloatProperty(
        name="Auto-close (s)",
        description="Seconds to stay open before auto-closing. 0 = stay open",
        default=3.0, min=0.0, soft_max=30.0,
    )


class SOOB_PG_path_node(PropertyGroup):
    order: IntProperty(
        name="Order",
        description="Sort key within the path group (0 = first)",
        default=0, min=0,
    )


class SOOB_PG_entity(PropertyGroup):
    """Per-object SOOB Engine entity properties.

    The `type` enum picks one of 11 behaviors; the nested PointerProperty
    sub-groups hold per-type fields. Sub-groups stay attached to every
    object regardless of type so switching the dropdown back and forth
    doesn't lose your values.
    """

    type: EnumProperty(
        name="Type",
        description="What kind of SOOB entity this object represents",
        items=SOOB_TYPE_ITEMS,
        default="none",
    )

    common:    PointerProperty(type=SOOB_PG_common)
    item:      PointerProperty(type=SOOB_PG_item)
    enemy:     PointerProperty(type=SOOB_PG_enemy)
    platform:  PointerProperty(type=SOOB_PG_platform)
    switch:    PointerProperty(type=SOOB_PG_switch)
    trigger:   PointerProperty(type=SOOB_PG_trigger)
    door:      PointerProperty(type=SOOB_PG_door)
    path_node: PointerProperty(type=SOOB_PG_path_node)


# ---------------------------------------------------------------------------
# UI panels
# ---------------------------------------------------------------------------

class SOOB_PT_object_panel(Panel):
    """Object Properties > SOOB Entity panel.

    Top-level dropdown picks the entity type; sub-panels reveal type-
    specific fields. We use one panel with a `draw_type_*` dispatch
    rather than 11 separate panels — fewer classes to register, and the
    UI reads as one cohesive section.
    """

    bl_label       = "SOOB Entity"
    bl_idname      = "SOOB_PT_object_panel"
    bl_space_type  = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context     = "object"

    @classmethod
    def poll(cls, context):
        return context.object is not None

    def draw(self, context):
        layout = self.layout
        soob = context.object.soob
        layout.use_property_split = True
        layout.prop(soob, "type")

        if soob.type == "none":
            layout.label(text="Exported as level geometry.", icon="MESH_DATA")
            return

        # ---- Common fields shown on every entity ------------------
        common = soob.common
        col = layout.column(align=True)
        col.prop(common, "name_override")
        col.prop(common, "group")
        col.separator()
        col.prop(common, "scale")
        col.prop(common, "static")
        col.prop(common, "flip_cull")
        col.prop(common, "collide")
        # Visual binding only matters for entities that draw a mesh —
        # waypoints / path_nodes / triggers / players don't.
        if soob.type in {"decoration", "item", "enemy", "platform",
                         "switch", "door"}:
            col.separator()
            col.label(text="Visual")
            col.prop(common, "mesh")
            col.prop(common, "tex")
            col.prop(common, "iqm")
            if common.iqm:
                col.prop(common, "anim")
                col.prop(common, "anim_speed")

        # ---- Type-specific fields ---------------------------------
        layout.separator()
        if soob.type == "item":
            layout.prop(soob.item, "item_type")
        elif soob.type == "enemy":
            col = layout.column(align=True)
            col.prop(soob.enemy, "health")
            col.prop(soob.enemy, "speed")
            col.prop(soob.enemy, "sight")
        elif soob.type == "platform":
            col = layout.column(align=True)
            col.prop(soob.platform, "path")
            col.prop(soob.platform, "move")
            col.prop(soob.platform, "speed")
            col.prop(soob.platform, "enabled")
            col.prop(soob.platform, "face_path")
        elif soob.type == "switch":
            layout.prop(soob.switch, "target")
        elif soob.type == "trigger":
            col = layout.column(align=True)
            col.prop(soob.trigger, "target")
            col.prop(soob.trigger, "once")
            col.separator()
            col.label(text="Trigger half-extent = Object Scale "
                           "(scale 1 = 2 m cube)",
                      icon="INFO")
        elif soob.type == "door":
            col = layout.column(align=True)
            col.prop(soob.door, "motion")
            col.prop(soob.door, "axis")
            col.prop(soob.door, "amount")
            col.prop(soob.door, "speed")
            col.prop(soob.door, "auto_close")
        elif soob.type == "path_node":
            layout.prop(soob.path_node, "order")
        elif soob.type == "waypoint":
            layout.label(text="Position-only nav node. No extra fields.",
                         icon="DECORATE_KEYFRAME")
        elif soob.type == "player":
            layout.label(text="Spawn position + yaw (rotation Z).",
                         icon="USER")


# ---------------------------------------------------------------------------
# Export operator
# ---------------------------------------------------------------------------

class SOOB_OT_export_level(Operator, ExportHelper):
    """Export the current scene as a SOOB Engine level"""
    bl_idname  = "export_scene.soob_level"
    bl_label   = "Export SOOB Level"
    bl_options = {"PRESET"}

    filename_ext = ".obj"
    filter_glob: StringProperty(default="*.obj", options={"HIDDEN"})

    lightmap_uv_name: StringProperty(
        name="Lightmap UV Map",
        description="Name of the UV layer used for `vt` output (engine "
                    "treats this as the lightmap UV). Falls back to the "
                    "active UV layer when missing",
        default="Lightmap",
    )

    bake_lightmaps: BoolProperty(
        name="Bake Lightmaps",
        description="Run a per-material Cycles bake before writing the OBJ. "
                    "Each material's lightmap is saved as <material>_lm.png "
                    "next to the .obj. Uncheck for fast geometry-only exports",
        default=True,
    )

    lightmap_resolution: EnumProperty(
        name="Lightmap Resolution",
        description="Per-material lightmap image size (square)",
        items=(
            ("128",  "128 x 128",  ""),
            ("256",  "256 x 256",  ""),
            ("512",  "512 x 512",  ""),
            ("1024", "1024 x 1024", ""),
        ),
        default="512",
    )

    bake_samples: IntProperty(
        name="Bake Samples",
        description="Cycles sample count for the bake (higher = cleaner, slower)",
        default=64, min=1, soft_max=512,
    )

    bake_margin: IntProperty(
        name="Bake Margin (px)",
        description="Pixel margin extruded outside UV islands. Hides seams "
                    "when the flashlight writes near island edges",
        default=4, min=0, soft_max=32,
    )

    auto_uv: BoolProperty(
        name="Auto-create Lightmap UVs",
        description="If the mesh has no UV map named like `Lightmap UV Map`, "
                    "run Smart UV Project on a new layer (margin 0.04)",
        default=True,
    )

    copy_assets: BoolProperty(
        name="Copy Referenced Assets",
        description="Resolve mesh/tex/iqm logical names via assets.lua and "
                    "copy the source files into the export tree. Existing "
                    "files at the destination are left alone",
        default=False,
    )

    @classmethod
    def poll(cls, context):
        return context.active_object is not None and context.active_object.type == "MESH"

    def execute(self, context):
        obj = context.active_object
        if obj is None or obj.type != "MESH":
            self.report({"ERROR"}, "SOOB export needs an active mesh object")
            return {"CANCELLED"}

        obj_path = self.filepath
        stem, _ = os.path.splitext(obj_path)
        mtl_path = stem + ".mtl"
        mtl_basename = os.path.basename(mtl_path)
        export_dir = os.path.dirname(obj_path) or "."

        # ---- Bake (before OBJ write so we fail fast on missing UVs) ----
        baked = []
        if self.bake_lightmaps:
            try:
                baked = _bake_lightmaps(
                    obj=obj,
                    export_dir=export_dir,
                    resolution=int(self.lightmap_resolution),
                    samples=self.bake_samples,
                    margin=self.bake_margin,
                    uv_name=self.lightmap_uv_name,
                    auto_uv=self.auto_uv,
                    uv_island_margin=0.04,
                )
            except Exception as exc:
                self.report({"ERROR"}, "SOOB bake failed: %s" % exc)
                print("[soob] bake failed:", exc)
                raise

        # ---- OBJ + MTL writers --------------------------------------
        depsgraph = context.evaluated_depsgraph_get()
        try:
            n_verts, n_faces, used_materials = _write_obj(
                obj, depsgraph, obj_path, mtl_basename, self.lightmap_uv_name
            )
        except Exception as exc:
            self.report({"ERROR"}, "SOOB export failed: %s" % exc)
            print("[soob] export failed:", exc)
            raise

        if used_materials:
            _write_mtl(used_materials, mtl_path)

        # ---- .ent writer ---------------------------------------------
        ent_path = stem + ".ent"
        ent_counts = _write_ent_file(
            [o for o in context.scene.objects], ent_path
        )
        ent_total = sum(ent_counts.values())

        # ---- Optional asset copy -------------------------------------
        copied = []
        if self.copy_assets:
            # Sources come from the repo containing assets.lua (walked
            # up from export_dir); destination is the export_dir itself.
            copied = _copy_referenced_assets(
                [o for o in context.scene.objects],
                src_root=export_dir,
                dst_root=export_dir,
            )

        suffix = ""
        if copied:
            suffix = ", %d assets copied" % len(copied)

        if used_materials:
            self.report(
                {"INFO"},
                "SOOB: wrote %s (%d verts, %d faces, %d materials, "
                "%d lightmaps, %d entities%s)" %
                (os.path.basename(obj_path), n_verts, n_faces,
                 len(used_materials), len(baked), ent_total, suffix),
            )
        else:
            self.report(
                {"INFO"},
                "SOOB: wrote %s (%d verts, %d faces, no materials, "
                "%d entities%s)" %
                (os.path.basename(obj_path), n_verts, n_faces, ent_total, suffix),
            )

        print("[soob] export ok -> %s" % obj_path)
        print("[soob] entities: %s" % ent_counts)
        return {"FINISHED"}


def menu_func_export(self, context):
    self.layout.operator(SOOB_OT_export_level.bl_idname, text="SOOB Level (.obj)")


_classes = (
    # PropertyGroups must be registered before any class that holds a
    # PointerProperty to them — and the parent group last.
    SOOB_PG_common,
    SOOB_PG_item,
    SOOB_PG_enemy,
    SOOB_PG_platform,
    SOOB_PG_switch,
    SOOB_PG_trigger,
    SOOB_PG_door,
    SOOB_PG_path_node,
    SOOB_PG_entity,

    SOOB_PT_object_panel,
    SOOB_PT_view3d_panel,
    SOOB_OT_export_level,
)


def register():
    for cls in _classes:
        bpy.utils.register_class(cls)
    # Property groups must be registered before the pointer that references
    # them. Attach `soob` to every Object so `obj.soob.type` works everywhere.
    bpy.types.Object.soob = PointerProperty(type=SOOB_PG_entity)
    bpy.types.Scene.soob_show_overlays = BoolProperty(
        name="Show Entity Overlays",
        description="Render path lines, trigger boxes, and entity markers "
                    "in the 3D viewport",
        default=True,
    )
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)

    # Install the world-space draw handler. Stored in a module-level cell
    # so unregister() can pull it back out — addons that lose track of
    # their handlers leak draw callbacks across reloads.
    if _DRAW_HANDLER[0] is None:
        _DRAW_HANDLER[0] = bpy.types.SpaceView3D.draw_handler_add(
            _overlay_draw, (), "WINDOW", "POST_VIEW"
        )


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)

    if _DRAW_HANDLER[0] is not None:
        try:
            bpy.types.SpaceView3D.draw_handler_remove(_DRAW_HANDLER[0], "WINDOW")
        except (ValueError, RuntimeError):
            pass
        _DRAW_HANDLER[0] = None

    # Delete the pointer before unregistering its target class, otherwise
    # Blender complains about a dangling RNA pointer on re-enable.
    if hasattr(bpy.types.Object, "soob"):
        del bpy.types.Object.soob
    if hasattr(bpy.types.Scene, "soob_show_overlays"):
        del bpy.types.Scene.soob_show_overlays
    for cls in reversed(_classes):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":
    register()
