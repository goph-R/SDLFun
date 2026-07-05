/*
 * edit_pick_test.cpp — Linux headless check for M1 selection + picking logic.
 *   g++ -I. -I../SOOB-Core editor/edit_pick_test.cpp -o /tmp/eptest && /tmp/eptest
 */
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>

static void conLogf(const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }

#include "edit_mesh.h"
#include "edit_select.h"
#include "edit_pick.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { \
    printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main(void)
{
    EditMesh m; editMeshInit(&m);
    memset(&m.mats[0], 0, sizeof(Material)); m.mats[0].tilingScale = 1.0f; m.numMats = 1;
    editAddCube(&m, 0.0f, 1.0f, 0.0f, 2.0f, 2.0f, 2.0f, 0);   /* corners x/z=+-1, y 0..2 */

    EditSelection sel; editSelInit(&sel, &m);
    CHECK(sel.mode == SEL_VERT);
    CHECK(sel.numEdges == 12);     /* a cube has 12 unique edges */
    CHECK(sel.numVerts == 8);
    CHECK(sel.numFaces == 6);

    /* Camera at (0,1,5) looking down -Z at the cube. */
    PickCam c;
    c.eye = editV3(0, 1, 5);
    c.forward = editV3(0, 0, -1);
    c.right = editV3(1, 0, 0);
    c.up = editV3(0, 1, 0);
    c.tanHalfFov = tanf(35.0f * 3.14159265f / 180.0f);
    c.aspect = 1.0f;
    c.vpW = 100; c.vpH = 100;

    /* Centre pixel ray hits the +Z face (z = 1). */
    int face = editPickFace(&c, &m, 50.0f, 50.0f);
    CHECK(face >= 0);
    if (face >= 0) {
        CHECK(m.faces[face].normal.z > 0.9f);   /* front (+Z) face */
    }

    /* Project a front-face corner, then pick it back. Vert 5 = (1,0,1). */
    float sx, sy;
    CHECK(editProject(&c, m.verts[5].pos, &sx, &sy) == 1);
    int v = editPickVertex(&c, &m, sx, sy, 6.0f);
    CHECK(v == 5);

    /* A point behind the camera does not project. */
    CHECK(editProject(&c, editV3(0, 1, 10), &sx, &sy) == 0);

    /* Occlusion: vert 0 = (-1,0,-1) is a BACK corner, hidden behind the front
       faces. Picking at its screen position must NOT return it (front-most +
       occlusion), and editOccluded must flag it while a front corner is clear. */
    CHECK(editOccluded(&m, c.eye, m.verts[0].pos) == 1);   /* back corner hidden */
    CHECK(editOccluded(&m, c.eye, m.verts[5].pos) == 0);   /* front corner clear */
    editProject(&c, m.verts[0].pos, &sx, &sy);
    CHECK(editPickVertex(&c, &m, sx, sy, 8.0f) != 0);      /* never the hidden vert */

    /* Pick an edge by aiming at the midpoint of the two front-top verts
       (6=(1,2,1), 7=(-1,2,1)); expect the edge {6,7}. */
    float ax, ay, bx, by;
    editProject(&c, m.verts[6].pos, &ax, &ay);
    editProject(&c, m.verts[7].pos, &bx, &by);
    int e = editPickEdge(&c, &m, sel.edges, sel.numEdges,
                         (ax + bx) * 0.5f, (ay + by) * 0.5f, 6.0f);
    CHECK(e >= 0);
    if (e >= 0) {
        int a = sel.edges[e].a, b = sel.edges[e].b;
        CHECK((a == 6 && b == 7) || (a == 7 && b == 6));
    }

    editSelFree(&sel); editMeshFree(&m);
    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
