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
#include "edit_ops.h"

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

    /* extrude a single face (the +Y top, index 4): +4 verts, +4 side walls */
    {
        EditMesh m; freshCube(&m);
        unsigned char sf[6];
        int i; for (i = 0; i < 6; i++) sf[i] = 0;
        sf[4] = 1;                                  /* top face */
        int n = editExtrude(&m, sf);
        CHECK(n == 1);
        CHECK(m.numVerts == 12);                    /* 4 cap verts duplicated */
        CHECK(m.numFaces == 10);                    /* 6 + 4 boundary walls */
        CHECK(m.faces[4].v[0] >= 8);                /* cap rewired to new verts */
        /* not moved yet -> cap normal still +Y */
        CHECK(m.faces[4].normal.y > 0.9f);
        editMeshFree(&m);
    }

    /* extrude two adjacent faces (+Y top #4 and +X #2): their shared edge is
       interior (no wall), so 6 boundary walls + 6 duplicated verts */
    {
        EditMesh m; freshCube(&m);
        unsigned char sf[6];
        int i; for (i = 0; i < 6; i++) sf[i] = 0;
        sf[4] = 1; sf[2] = 1;
        int n = editExtrude(&m, sf);
        CHECK(n == 2);
        CHECK(m.numVerts == 14);                    /* union of 2 quads = 6 verts */
        CHECK(m.numFaces == 12);                    /* 6 + 6 walls (1 shared edge) */
        editMeshFree(&m);
    }

    /* merge-by-distance: a quad sharing two coincident corners with the cube
       welds those two verts (12 -> 10), keeping all 7 faces. */
    {
        EditMesh m; freshCube(&m);                  /* verts 5=(1,0,1) 6=(1,2,1) */
        editAddQuad(&m, V(1,0,1), V(1,2,1), V(3,2,1), V(3,0,1), 0);
        CHECK(m.numVerts == 12);
        CHECK(m.numFaces == 7);
        int removed = editMergeByDistance(&m, EDIT_SNAP * 0.5f);
        CHECK(removed == 2);
        CHECK(m.numVerts == 10);
        CHECK(m.numFaces == 7);                     /* nothing collapsed */
        editMeshFree(&m);
    }

    /* merge collapses a degenerate quad: a quad with two coincident corners
       (a,a,b,c) welds to a triangle, faces stay (the tri survives). */
    {
        EditMesh m; editMeshInit(&m);
        memset(&m.mats[0], 0, sizeof(Material)); m.mats[0].tilingScale = 1.0f; m.numMats = 1;
        editAddQuad(&m, V(0,0,0), V(0,0,0.002f), V(1,0,0), V(1,0,1), 0);  /* 0~1 */
        CHECK(m.numVerts == 4);
        int removed = editMergeByDistance(&m, EDIT_SNAP * 0.5f);
        CHECK(removed == 1);
        CHECK(m.numVerts == 3);
        CHECK(m.numFaces == 1);
        CHECK(m.faces[0].nv == 3);                  /* quad -> tri */
        editMeshFree(&m);
    }

    /* recalc-outside: flip one face of a closed cube inward, recalc fixes it so
       every face normal points away from the cube centre (0,1,0). */
    {
        EditMesh m; freshCube(&m);
        editFlipFace(&m, 4);                        /* top face now inward */
        int flips = editRecalcNormalsConsistent(&m);
        CHECK(flips >= 1);
        Vec3 ctr = V(0, 1, 0);
        int i, j, allOut = 1;
        for (i = 0; i < m.numFaces; i++) {
            EditFace *f = &m.faces[i];
            Vec3 c = V(0,0,0);
            for (j = 0; j < f->nv; j++) {
                c.x += m.verts[f->v[j]].pos.x; c.y += m.verts[f->v[j]].pos.y;
                c.z += m.verts[f->v[j]].pos.z;
            }
            c.x /= f->nv; c.y /= f->nv; c.z /= f->nv;
            float d = f->normal.x*(c.x-ctr.x) + f->normal.y*(c.y-ctr.y) + f->normal.z*(c.z-ctr.z);
            if (d <= 0.0f) allOut = 0;
        }
        CHECK(allOut == 1);
        editMeshFree(&m);
    }

    /* recalc on an already-outward cube is a no-op (0 flips), stays outward. */
    {
        EditMesh m; freshCube(&m);
        int flips = editRecalcNormalsConsistent(&m);
        CHECK(flips == 0);
        editMeshFree(&m);
    }

    /* recalc on an open shell (single quad) only enforces consistency — it must
       not spuriously flip the lone face (no closed volume to orient to). */
    {
        EditMesh m; editMeshInit(&m);
        memset(&m.mats[0], 0, sizeof(Material)); m.mats[0].tilingScale = 1.0f; m.numMats = 1;
        editAddQuad(&m, V(0,0,0), V(1,0,0), V(1,0,1), V(0,0,1), 0);
        Vec3 before = m.faces[0].normal;
        int flips = editRecalcNormalsConsistent(&m);
        CHECK(flips == 0);
        CHECK(m.faces[0].normal.y * before.y > 0.0f);   /* same side */
        editMeshFree(&m);
    }

    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
