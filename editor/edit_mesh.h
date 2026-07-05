#ifndef EDIT_MESH_H
#define EDIT_MESH_H

/* ---- Editor native mesh (M0 core) -----------------------------------------
 *
 * The editor's document is an editable indexed mesh: shared verts + tri/quad
 * faces, each face tagged with a material. It's deliberately NOT a half-edge /
 * BMesh structure (pointer-heavy, painful without C++11) — adjacency is
 * computed on demand by the ops that need it (extrude, edge selection).
 *
 * Two invariants make later ops cheap and robust:
 *   - Every vertex position is snapped to EDIT_SNAP (1 cm) on write, so vertex
 *     identity is exact (weld/merge quantize to the same cell) and T-junction
 *     hairline cracks mostly vanish.
 *   - Diffuse textures tile via world-space box-mapping (see computeTilingUV in
 *     render_level.h), so geometry needs NO UVs — nothing here authors any.
 *
 * Pure data: no GL, no FLTK. Include after obj_loader.h (for Vec3 / Material /
 * OBJ_MAX_MATERIALS). Testable on Linux headless (edit_mesh_test.cpp).
 * -------------------------------------------------------------------------- */

#include <cstdlib>
#include <cmath>
#include "obj_loader.h"      /* Vec3, Material, OBJ_MAX_MATERIALS */

#define EDIT_SNAP 0.01f      /* 1 cm authoring grid */

/* Round to the nearest EDIT_SNAP increment. Deterministic: the same input
   always yields the same output, so two verts that should coincide quantize
   identically (basis for exact weld / T-junction avoidance). */
static float editSnap(float x)
{
    return floorf(x / EDIT_SNAP + 0.5f) * EDIT_SNAP;
}

typedef struct {
    Vec3 pos;                /* always snapped to EDIT_SNAP on write */
} EditVert;

typedef struct {
    int  v[4];               /* vertex indices; tri leaves v[3] = -1 */
    int  nv;                 /* 3 or 4 */
    int  materialId;         /* index into mats[], -1 = none */
    Vec3 normal;             /* cached face normal, recomputed on edit */
} EditFace;

typedef struct {
    EditVert *verts; int numVerts, capVerts;
    EditFace *faces; int numFaces, capFaces;
    Material  mats[OBJ_MAX_MATERIALS];
    int       numMats;
} EditMesh;

static void editMeshInit(EditMesh *m)
{
    m->capVerts = 256;
    m->capFaces = 256;
    m->verts = (EditVert *)malloc(m->capVerts * sizeof(EditVert));
    m->faces = (EditFace *)malloc(m->capFaces * sizeof(EditFace));
    m->numVerts = 0;
    m->numFaces = 0;
    m->numMats  = 0;
}

static void editMeshFree(EditMesh *m)
{
    free(m->verts);
    free(m->faces);
    m->verts = NULL; m->faces = NULL;
    m->numVerts = m->numFaces = 0;
    m->capVerts = m->capFaces = 0;
}

/* Add a vertex (snapped to 1 cm). Returns its index. */
static int editAddVert(EditMesh *m, float x, float y, float z)
{
    if (m->numVerts >= m->capVerts) {
        m->capVerts *= 2;
        m->verts = (EditVert *)realloc(m->verts, m->capVerts * sizeof(EditVert));
    }
    m->verts[m->numVerts].pos.x = editSnap(x);
    m->verts[m->numVerts].pos.y = editSnap(y);
    m->verts[m->numVerts].pos.z = editSnap(z);
    return m->numVerts++;
}

/* Recompute a face's cached normal from its first three verts (edge cross,
   normalized) — robust enough for planar tris/quads. */
static void editFaceComputeNormal(EditMesh *m, EditFace *f)
{
    Vec3 *a = &m->verts[f->v[0]].pos;
    Vec3 *b = &m->verts[f->v[1]].pos;
    Vec3 *c = &m->verts[f->v[2]].pos;
    float e1x = b->x - a->x, e1y = b->y - a->y, e1z = b->z - a->z;
    float e2x = c->x - a->x, e2y = c->y - a->y, e2z = c->z - a->z;
    float nx = e1y * e2z - e1z * e2y;
    float ny = e1z * e2x - e1x * e2z;
    float nz = e1x * e2y - e1y * e2x;
    float l = sqrtf(nx * nx + ny * ny + nz * nz);
    if (l > 1e-8f) { nx /= l; ny /= l; nz /= l; }
    f->normal.x = nx; f->normal.y = ny; f->normal.z = nz;
}

/* Add a face (tri if v3 < 0, else quad). Winding is taken as given (CCW as seen
   from outside); the cached normal is computed to match. Returns face index. */
static int editAddFace(EditMesh *m, int v0, int v1, int v2, int v3, int matId)
{
    if (m->numFaces >= m->capFaces) {
        m->capFaces *= 2;
        m->faces = (EditFace *)realloc(m->faces, m->capFaces * sizeof(EditFace));
    }
    EditFace *f = &m->faces[m->numFaces];
    f->v[0] = v0; f->v[1] = v1; f->v[2] = v2; f->v[3] = v3;
    f->nv = (v3 < 0) ? 3 : 4;
    f->materialId = matId;
    editFaceComputeNormal(m, f);
    return m->numFaces++;
}

/* Reverse a face's winding (flip normal). */
static void editFlipFace(EditMesh *m, int faceIdx)
{
    EditFace *f = &m->faces[faceIdx];
    if (f->nv == 3) {
        int t = f->v[1]; f->v[1] = f->v[2]; f->v[2] = t;
    } else {
        int a = f->v[0], b = f->v[1], c = f->v[2], d = f->v[3];
        f->v[0] = a; f->v[1] = d; f->v[2] = c; f->v[3] = b;
    }
    editFaceComputeNormal(m, f);
}

/* Seed an axis-aligned box centred at (cx,cy,cz) with full sizes sx,sy,sz.
   Emits 8 shared verts + 6 outward-wound quad faces. (Add > Cube.) */
static void editAddCube(EditMesh *m, float cx, float cy, float cz,
                        float sx, float sy, float sz, int matId)
{
    float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
    int b = m->numVerts;
    editAddVert(m, cx - hx, cy - hy, cz - hz); /* 0 */
    editAddVert(m, cx + hx, cy - hy, cz - hz); /* 1 */
    editAddVert(m, cx + hx, cy + hy, cz - hz); /* 2 */
    editAddVert(m, cx - hx, cy + hy, cz - hz); /* 3 */
    editAddVert(m, cx - hx, cy - hy, cz + hz); /* 4 */
    editAddVert(m, cx + hx, cy - hy, cz + hz); /* 5 */
    editAddVert(m, cx + hx, cy + hy, cz + hz); /* 6 */
    editAddVert(m, cx - hx, cy + hy, cz + hz); /* 7 */
    editAddFace(m, b + 4, b + 5, b + 6, b + 7, matId); /* +Z front  */
    editAddFace(m, b + 1, b + 0, b + 3, b + 2, matId); /* -Z back   */
    editAddFace(m, b + 5, b + 1, b + 2, b + 6, matId); /* +X right  */
    editAddFace(m, b + 0, b + 4, b + 7, b + 3, matId); /* -X left   */
    editAddFace(m, b + 3, b + 7, b + 6, b + 2, matId); /* +Y top    */
    editAddFace(m, b + 0, b + 1, b + 5, b + 4, matId); /* -Y bottom */
}

/* Seed a single quad from four corners (CCW from the front). (Add > Plane.) */
static void editAddQuad(EditMesh *m, Vec3 a, Vec3 b, Vec3 c, Vec3 d, int matId)
{
    int i0 = editAddVert(m, a.x, a.y, a.z);
    int i1 = editAddVert(m, b.x, b.y, b.z);
    int i2 = editAddVert(m, c.x, c.y, c.z);
    int i3 = editAddVert(m, d.x, d.y, d.z);
    editAddFace(m, i0, i1, i2, i3, matId);
}

#endif /* EDIT_MESH_H */
