"""SOOB Engine — Blender level exporter.

One-click export of level geometry, lightmaps, and entities to the
engine's OBJ + MTL + .ent format. See `docs/PLAN_BLENDER.md` for the
full design.

Installation:
    Blender > Edit > Preferences > Add-ons > Install...
    Pick this file, enable "SOOB Engine - Level Exporter".
    The export option appears under File > Export > SOOB Level (.obj).

Status: in-progress. OBJ + MTL writer works for level geometry from the
active mesh; lightmap bake, entity writers, and viewport overlays land
in later implementation steps (see PLAN_BLENDER.md §"Implementation
phases").
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
    IntProperty,
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


class SOOB_PG_entity(PropertyGroup):
    """Per-object SOOB Engine entity properties.

    Type-specific sub-groups (platform / trigger / door / ...) will be
    nested into this group in later steps. For now only the type
    discriminator exists.
    """

    type: EnumProperty(
        name="Type",
        description="What kind of SOOB entity this object represents",
        items=SOOB_TYPE_ITEMS,
        default="none",
    )


# ---------------------------------------------------------------------------
# UI panels
# ---------------------------------------------------------------------------

class SOOB_PT_object_panel(Panel):
    """Object Properties > SOOB Entity panel.

    The dropdown lives here on every object; non-"none" picks will reveal
    sub-panels for type-specific fields once Step 5 lands.
    """

    bl_label       = "SOOB Entity"
    bl_idname      = "SOOB_PT_object_panel"
    bl_space_type  = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context     = "object"

    @classmethod
    def poll(cls, context):
        # Hide the panel on objects Blender doesn't carry data for (lights,
        # cameras still get it — those will simply stay "none" and be
        # ignored by the exporter).
        return context.object is not None

    def draw(self, context):
        layout = self.layout
        soob = context.object.soob
        layout.use_property_split = True
        layout.prop(soob, "type")

        if soob.type == "none":
            layout.label(text="Exported as level geometry.", icon="MESH_DATA")


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
            self.report(
                {"INFO"},
                "SOOB: wrote %s (%d verts, %d faces, %d materials, %d lightmaps)" %
                (os.path.basename(obj_path), n_verts, n_faces,
                 len(used_materials), len(baked)),
            )
        else:
            # Single-material / no-material level — engine falls back to
            # assets/levels/diffuse.png + lightmap.png. Skip MTL entirely.
            self.report(
                {"INFO"},
                "SOOB: wrote %s (%d verts, %d faces, no materials)" %
                (os.path.basename(obj_path), n_verts, n_faces),
            )

        print("[soob] export ok -> %s" % obj_path)
        return {"FINISHED"}


def menu_func_export(self, context):
    self.layout.operator(SOOB_OT_export_level.bl_idname, text="SOOB Level (.obj)")


_classes = (
    SOOB_PG_entity,
    SOOB_PT_object_panel,
    SOOB_OT_export_level,
)


def register():
    for cls in _classes:
        bpy.utils.register_class(cls)
    # Property groups must be registered before the pointer that references
    # them. Attach `soob` to every Object so `obj.soob.type` works everywhere.
    bpy.types.Object.soob = PointerProperty(type=SOOB_PG_entity)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    # Delete the pointer before unregistering its target class, otherwise
    # Blender complains about a dangling RNA pointer on re-enable.
    if hasattr(bpy.types.Object, "soob"):
        del bpy.types.Object.soob
    for cls in reversed(_classes):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":
    register()
