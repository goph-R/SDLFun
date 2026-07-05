#ifndef EDIT_MESH_BUILD_H
#define EDIT_MESH_BUILD_H

/* ---- EditMesh -> ObjMesh (the renderer's format) --------------------------
 *
 * Triangulates the editor's native mesh into the engine's ObjMesh so the exact
 * game draw path (renderLevelSectored) can render it. Key choices:
 *
 *   - Verts are copied shared (indexed), so moving a vert later moves every
 *     face that uses it — the "Blender feel".
 *   - Each face contributes ONE flat normal, shared by its triangles: this
 *     gives correct box-mapping (computeTilingUV picks the projection axis from
 *     the face normal) and flat per-face GL lighting.
 *   - No texcoords are emitted (numTexcoords stays 0): diffuse UVs are generated
 *     at draw time by box-mapping, and the lightmap unit is simply unused in the
 *     editor (lightmaps are still baked offline in Blender on export).
 *   - Materials are copied across, then objBuildSectors() batches triangles by
 *     material into the sectors renderLevelSectored iterates.
 *
 * `out` must already be objInit()'d. Safe to call repeatedly on the same `out`
 * (rebuild-on-edit): it resets the counts and refills, reusing the buffers.
 *
 * Include after obj_loader.h and edit_mesh.h.
 * -------------------------------------------------------------------------- */

#include "obj_loader.h"
#include "edit_mesh.h"

static void editMeshBuild(EditMesh *m, ObjMesh *out)
{
    out->numVerts     = 0;
    out->numNormals   = 0;
    out->numTexcoords = 0;
    out->numTris      = 0;
    out->numSectors   = 0;

    int i;
    for (i = 0; i < m->numVerts; i++)
        objAddVert(out, m->verts[i].pos.x, m->verts[i].pos.y, m->verts[i].pos.z);

    for (i = 0; i < m->numFaces; i++) {
        EditFace *f = &m->faces[i];
        editFaceComputeNormal(m, f);            /* keep the cache current */
        int ni = out->numNormals;
        objAddNormal(out, f->normal.x, f->normal.y, f->normal.z);

        /* Fan triangulation: (0,1,2) and, for quads, (0,2,3). t = -1 => no
           lightmap UVs; all three verts share the one flat face normal. */
        objAddTri(out, f->v[0], f->v[1], f->v[2],
                  -1, -1, -1, ni, ni, ni, f->materialId);
        if (f->nv == 4)
            objAddTri(out, f->v[0], f->v[2], f->v[3],
                      -1, -1, -1, ni, ni, ni, f->materialId);
    }

    out->numMaterials = m->numMats;
    for (i = 0; i < m->numMats; i++)
        out->materials[i] = m->mats[i];

    objBuildSectors(out);                       /* sort by material -> sectors */
}

#endif /* EDIT_MESH_BUILD_H */
