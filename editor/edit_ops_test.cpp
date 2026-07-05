/*
 * edit_ops_test.cpp — Linux headless check for M3 mesh ops (compaction/flip).
 *   g++ -I. -I../SOOB-Core editor/edit_ops_test.cpp -o /tmp/eotest && /tmp/eotest
 */
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>

static void conLogf(const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }

#include "edit_mesh.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { \
    printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static Vec3 V(float x, float y, float z) { Vec3 v; v.x = x; v.y = y; v.z = z; return v; }

static void freshCube(EditMesh *m)
{
    editMeshInit(m);
    memset(&m->mats[0], 0, sizeof(Material)); m->mats[0].tilingScale = 1.0f; m->numMats = 1;
    editAddCube(m, 0, 1, 0, 2, 2, 2, 0);   /* 8 verts, 6 quads */
}

int main(void)
{
    /* delete a vertex -> the 3 faces sharing that corner drop with it */
    {
        EditMesh m; freshCube(&m);
        unsigned char kv[8], kf[6];
        int i; for (i = 0; i < 8; i++) kv[i] = 1;
        for (i = 0; i < 6; i++) kf[i] = 1;
        kv[0] = 0;                                  /* corner 0 */
        editMeshCompact(&m, kv, kf);
        CHECK(m.numVerts == 7);
        CHECK(m.numFaces == 3);                     /* corner 0 is on 3 faces */
        editMeshFree(&m);
    }

    /* delete one face, keep all verts -> 5 faces, 8 verts (shared, no orphans) */
    {
        EditMesh m; freshCube(&m);
        unsigned char kv[8], kf[6];
        int i; for (i = 0; i < 8; i++) kv[i] = 1;
        for (i = 0; i < 6; i++) kf[i] = 1;
        kf[0] = 0;
        editMeshCompact(&m, kv, kf);
        CHECK(m.numVerts == 8);
        CHECK(m.numFaces == 5);
        editMeshFree(&m);
    }

    /* orphan-vert removal: delete a face, then keep only verts still used */
    {
        EditMesh m; freshCube(&m);
        /* add an isolated quad far away (its 4 verts are used only by it) */
        editAddQuad(&m, V(10,0,0), V(11,0,0), V(11,0,1), V(10,0,1), 0);
        CHECK(m.numVerts == 12);
        CHECK(m.numFaces == 7);
        unsigned char kv[12], kf[7];
        int i, j;
        for (i = 0; i < 12; i++) kv[i] = 0;
        for (i = 0; i < 7; i++)  kf[i] = 1;
        kf[6] = 0;                                  /* drop the isolated quad */
        for (i = 0; i < m.numFaces; i++)            /* keep verts of surviving faces */
            if (kf[i]) for (j = 0; j < m.faces[i].nv; j++) kv[m.faces[i].v[j]] = 1;
        editMeshCompact(&m, kv, kf);
        CHECK(m.numVerts == 8);                     /* the 4 orphans are gone */
        CHECK(m.numFaces == 6);
        editMeshFree(&m);
    }

    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
