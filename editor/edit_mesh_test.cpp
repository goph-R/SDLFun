/*
 * edit_mesh_test.cpp — Linux headless sanity check for the editor mesh core.
 *
 * The M0 logic (edit_mesh.h / edit_mesh_build.h) is pure data — no GL, no FLTK —
 * so it can be validated fast off-Windows before the FLTK/GL integration is
 * even built. Run from the repo root:
 *
 *     g++ -I. -I../SOOB-Core editor/edit_mesh_test.cpp -o /tmp/emtest && /tmp/emtest
 */

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>

/* obj_loader.h's objBuildSectors() calls conLogf(); provide a stdout sink. */
static void conLogf(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
}

#include "edit_mesh.h"
#include "edit_mesh_build.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { \
    printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
#define NEAR(a, b) (fabsf((float)(a) - (float)(b)) < 1e-4f)

int main(void)
{
    EditMesh m;
    editMeshInit(&m);

    /* one material so objBuildSectors produces a sector */
    memset(&m.mats[0], 0, sizeof(Material));
    strcpy(m.mats[0].name, "test");
    m.mats[0].tilingScale = 1.0f;
    m.numMats = 1;

    /* 2 m cube centred at y = 1 -> corners land exactly on the 1 cm grid */
    editAddCube(&m, 0.0f, 1.0f, 0.0f, 2.0f, 2.0f, 2.0f, 0);
    CHECK(m.numVerts == 8);
    CHECK(m.numFaces == 6);
    CHECK(NEAR(m.verts[0].pos.y, 0.0f));   /* 1 - 1 */
    CHECK(NEAR(m.verts[2].pos.y, 2.0f));   /* 1 + 1 */

    /* every cube face normal must point outward (away from the centre) */
    for (int i = 0; i < m.numFaces; i++) {
        EditFace *f = &m.faces[i];
        Vec3 c = {0, 0, 0};
        for (int j = 0; j < f->nv; j++) {
            c.x += m.verts[f->v[j]].pos.x;
            c.y += m.verts[f->v[j]].pos.y;
            c.z += m.verts[f->v[j]].pos.z;
        }
        c.x /= f->nv; c.y /= f->nv; c.z /= f->nv;   /* face centroid */
        /* vector from cube centre (0,1,0) to the face centroid */
        float ox = c.x - 0.0f, oy = c.y - 1.0f, oz = c.z - 0.0f;
        float dot = f->normal.x * ox + f->normal.y * oy + f->normal.z * oz;
        CHECK(dot > 0.0f);                          /* normal agrees = outward */
    }

    /* triangulation into the engine mesh */
    ObjMesh o;
    objInit(&o);
    editMeshBuild(&m, &o);
    CHECK(o.numVerts == 8);
    CHECK(o.numNormals == 6);      /* one flat normal per face */
    CHECK(o.numTris == 12);        /* 6 quads -> 12 tris */
    CHECK(o.numMaterials == 1);
    CHECK(o.numSectors == 1);
    CHECK(o.sectors[0].triCount == 12);

    /* rebuild-on-edit: calling build again must not accumulate */
    editMeshBuild(&m, &o);
    CHECK(o.numVerts == 8);
    CHECK(o.numTris == 12);

    /* snapping rounds arbitrary input to the 1 cm grid */
    int vi = editAddVert(&m, 1.2349f, -0.005f, 3.001f);
    CHECK(NEAR(m.verts[vi].pos.x, 1.23f));
    CHECK(NEAR(m.verts[vi].pos.y, 0.00f));
    CHECK(NEAR(m.verts[vi].pos.z, 3.00f));

    /* flip reverses winding -> normal negates */
    Vec3 n0 = m.faces[0].normal;
    editFlipFace(&m, 0);
    CHECK(NEAR(m.faces[0].normal.x, -n0.x));
    CHECK(NEAR(m.faces[0].normal.y, -n0.y));
    CHECK(NEAR(m.faces[0].normal.z, -n0.z));

    objFree(&o);
    editMeshFree(&m);

    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
