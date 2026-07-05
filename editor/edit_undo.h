#ifndef EDIT_UNDO_H
#define EDIT_UNDO_H

/* ---- Editor undo/redo (M2) ------------------------------------------------
 *
 * Whole-mesh snapshots. Editor meshes are small, so a full deep copy before
 * each edit is the simplest bulletproof scheme (no per-op inverse logic). Two
 * bounded stacks: `undo` holds past states, `redo` holds states popped by undo.
 *
 *   editHistoryPush(h, mesh)  -- call BEFORE a mutating op; snapshots `mesh`
 *                                and clears the redo stack.
 *   editHistoryUndo(h, mesh)  -- push current to redo, restore top undo.
 *   editHistoryRedo(h, mesh)  -- symmetric.
 *   editHistoryDropUndoTop(h) -- discard the last push (e.g. a cancelled grab).
 *
 * Restore transfers array ownership (no extra copy). After a restore the caller
 * must rebuild the render mesh (editMeshBuild) and reconcile selection sizes.
 *
 * Pure data: no GL, no FLTK. Include after edit_mesh.h.
 * -------------------------------------------------------------------------- */

#include <cstdlib>
#include <cstring>
#include "edit_mesh.h"

#define EDIT_UNDO_MAX 64

/* Deep copy `src` into `dst` with exact-fit arrays. `dst` is overwritten (its
   prior contents are NOT freed — pass a fresh/empty slot). */
static void editMeshCopy(EditMesh *dst, const EditMesh *src)
{
    int nv = src->numVerts, nf = src->numFaces, i;
    dst->capVerts = nv > 0 ? nv : 1;
    dst->capFaces = nf > 0 ? nf : 1;
    dst->verts = (EditVert *)malloc(dst->capVerts * sizeof(EditVert));
    dst->faces = (EditFace *)malloc(dst->capFaces * sizeof(EditFace));
    memcpy(dst->verts, src->verts, nv * sizeof(EditVert));
    memcpy(dst->faces, src->faces, nf * sizeof(EditFace));
    dst->numVerts = nv;
    dst->numFaces = nf;
    dst->numMats  = src->numMats;
    for (i = 0; i < src->numMats; i++) dst->mats[i] = src->mats[i];
}

typedef struct {
    EditMesh undo[EDIT_UNDO_MAX]; int nUndo;
    EditMesh redo[EDIT_UNDO_MAX]; int nRedo;
} EditHistory;

static void editHistoryInit(EditHistory *h) { h->nUndo = 0; h->nRedo = 0; }

static void editHistoryClearRedo(EditHistory *h)
{
    int i;
    for (i = 0; i < h->nRedo; i++) editMeshFree(&h->redo[i]);
    h->nRedo = 0;
}

static void editHistoryFree(EditHistory *h)
{
    int i;
    for (i = 0; i < h->nUndo; i++) editMeshFree(&h->undo[i]);
    for (i = 0; i < h->nRedo; i++) editMeshFree(&h->redo[i]);
    h->nUndo = h->nRedo = 0;
}

/* Snapshot `cur` as a restore point. Call BEFORE mutating the mesh. Clears the
   redo stack (a new edit invalidates the redo future). */
static void editHistoryPush(EditHistory *h, const EditMesh *cur)
{
    editHistoryClearRedo(h);
    if (h->nUndo == EDIT_UNDO_MAX) {          /* drop oldest */
        int i;
        editMeshFree(&h->undo[0]);
        for (i = 1; i < h->nUndo; i++) h->undo[i - 1] = h->undo[i];
        h->nUndo--;
    }
    editMeshCopy(&h->undo[h->nUndo++], cur);
}

/* Discard the most recent undo point (e.g. a grab that was cancelled, leaving
   the mesh already back at the pre-grab state). */
static void editHistoryDropUndoTop(EditHistory *h)
{
    if (h->nUndo > 0) editMeshFree(&h->undo[--h->nUndo]);
}

/* Undo: stash the current mesh on redo, restore the top undo snapshot into
   `mesh`. Returns 1 if a state was restored, 0 if the undo stack was empty. */
static int editHistoryUndo(EditHistory *h, EditMesh *mesh)
{
    if (h->nUndo == 0) return 0;
    editMeshCopy(&h->redo[h->nRedo++], mesh);
    editMeshFree(mesh);
    *mesh = h->undo[--h->nUndo];              /* transfer ownership */
    return 1;
}

static int editHistoryRedo(EditHistory *h, EditMesh *mesh)
{
    if (h->nRedo == 0) return 0;
    editMeshCopy(&h->undo[h->nUndo++], mesh);
    editMeshFree(mesh);
    *mesh = h->redo[--h->nRedo];
    return 1;
}

/* Restore the top undo snapshot into `mesh` and discard it, with NO redo entry.
   Used to abort an in-progress op (e.g. cancel an extrude+move as one step). */
static int editHistoryPopRestore(EditHistory *h, EditMesh *mesh)
{
    if (h->nUndo == 0) return 0;
    editMeshFree(mesh);
    *mesh = h->undo[--h->nUndo];
    return 1;
}

#endif /* EDIT_UNDO_H */
