/*
 * edit_undo_test.cpp — Linux headless check for the M2 undo/redo stack.
 *   g++ -I. -I../SOOB-Core editor/edit_undo_test.cpp -o /tmp/eutest && /tmp/eutest
 */
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>

static void conLogf(const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }

#include "edit_mesh.h"
#include "edit_undo.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { \
    printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
#define NEAR(a, b) (fabsf((float)(a) - (float)(b)) < 1e-4f)

int main(void)
{
    EditMesh m; editMeshInit(&m);
    memset(&m.mats[0], 0, sizeof(Material)); m.mats[0].tilingScale = 1.0f; m.numMats = 1;
    editAddCube(&m, 0, 1, 0, 2, 2, 2, 0);
    float y0 = m.verts[2].pos.y;            /* a top corner, y = 2 */

    EditHistory h; editHistoryInit(&h);

    /* edit #1: push, then move a vertex */
    editHistoryPush(&h, &m);
    m.verts[2].pos.y = editSnap(y0 + 0.50f);
    CHECK(NEAR(m.verts[2].pos.y, 2.5f));
    CHECK(h.nUndo == 1);

    /* undo -> back to y0 */
    CHECK(editHistoryUndo(&h, &m) == 1);
    CHECK(NEAR(m.verts[2].pos.y, y0));
    CHECK(h.nUndo == 0 && h.nRedo == 1);

    /* redo -> moved again */
    CHECK(editHistoryRedo(&h, &m) == 1);
    CHECK(NEAR(m.verts[2].pos.y, 2.5f));
    CHECK(h.nUndo == 1 && h.nRedo == 0);

    /* a fresh push clears redo */
    CHECK(editHistoryUndo(&h, &m) == 1);    /* nRedo -> 1 */
    editHistoryPush(&h, &m);
    CHECK(h.nRedo == 0);

    /* empty-stack calls are no-ops */
    while (editHistoryUndo(&h, &m)) {}
    CHECK(editHistoryUndo(&h, &m) == 0);

    /* dropUndoTop discards a snapshot (cancelled-grab path) */
    editHistoryPush(&h, &m);
    int before = h.nUndo;
    editHistoryDropUndoTop(&h);
    CHECK(h.nUndo == before - 1);

    /* structural integrity after a copy round-trip */
    editHistoryPush(&h, &m);
    editHistoryUndo(&h, &m);
    CHECK(m.numVerts == 8);
    CHECK(m.numFaces == 6);

    editHistoryFree(&h);
    editMeshFree(&m);
    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
