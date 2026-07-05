#ifndef EDIT_OPS_H
#define EDIT_OPS_H

/* ---- Editor mesh operations (M4: extrude) ---------------------------------
 *
 * editExtrude(): the core modeling verb. Given the faces flagged in selFace[],
 * it:
 *   1. duplicates every vertex those faces use (old -> new map),
 *   2. stitches a side quad along each BOUNDARY edge — an edge used by exactly
 *      one selected face (interior edges, shared by two selected faces, get no
 *      wall so the patch stays welded to itself),
 *   3. rewires the selected faces onto the duplicated verts (the "cap"),
 * leaving the cap detached from the surrounding mesh, connected only by the new
 * side walls. The cap faces KEEP their original indices (rewired in place);
 * walls are appended. The caller then moves the cap (auto-grab) to give the
 * extrusion its height — until then the walls are zero-area, which is fine.
 *
 * Boundary detection runs on the pre-rewire (old) indices; a directed edge is
 * interior iff its undirected pair appears twice among the selected faces'
 * edges. Side-quad winding (a, b, dup[b], dup[a]) — where a->b is the edge as
 * it runs in its cap face — yields an outward-facing wall.
 *
 * Pure data: no GL, no FLTK. Include after edit_mesh.h. The O(E^2) boundary
 * scan is fine for the modest face-counts an extrude selects.
 * -------------------------------------------------------------------------- */

#include <cstdlib>
#include "edit_mesh.h"

static int editExtrude(EditMesh *m, const unsigned char *selFace)
{
    int nf = m->numFaces, nv = m->numVerts, i, j;

    int nsel = 0;
    for (i = 0; i < nf; i++) if (selFace[i]) nsel++;
    if (nsel == 0) return 0;

    /* 1. duplicate every vert used by a selected face */
    int *dup = (int *)malloc((nv > 0 ? nv : 1) * sizeof(int));
    for (i = 0; i < nv; i++) dup[i] = -1;
    for (i = 0; i < nf; i++) {
        if (!selFace[i]) continue;
        EditFace *f = &m->faces[i];
        for (j = 0; j < f->nv; j++) {
            int v = f->v[j];
            if (dup[v] < 0)
                dup[v] = editAddVert(m, m->verts[v].pos.x,
                                        m->verts[v].pos.y, m->verts[v].pos.z);
        }
    }

    /* 2. gather the selected faces' directed edges (old indices), then wall
       every boundary edge. Do this BEFORE rewiring — a,b are still old. */
    int capEdges = 0;
    for (i = 0; i < nf; i++) if (selFace[i]) capEdges += m->faces[i].nv;
    int *ea   = (int *)malloc((capEdges > 0 ? capEdges : 1) * sizeof(int));
    int *eb   = (int *)malloc((capEdges > 0 ? capEdges : 1) * sizeof(int));
    int *emat = (int *)malloc((capEdges > 0 ? capEdges : 1) * sizeof(int));
    int ne = 0;
    for (i = 0; i < nf; i++) {
        if (!selFace[i]) continue;
        EditFace *f = &m->faces[i];
        for (j = 0; j < f->nv; j++) {
            ea[ne] = f->v[j];
            eb[ne] = f->v[(j + 1) % f->nv];
            emat[ne] = f->materialId;
            ne++;
        }
    }
    for (i = 0; i < ne; i++) {
        int a = ea[i], b = eb[i], count = 0, k;
        for (k = 0; k < ne; k++)
            if ((ea[k] == a && eb[k] == b) || (ea[k] == b && eb[k] == a)) count++;
        if (count == 1)                                   /* boundary edge */
            editAddFace(m, a, b, dup[b], dup[a], emat[i]);
    }

    /* 3. rewire the selected faces onto their duplicated verts */
    for (i = 0; i < nf; i++) {
        if (!selFace[i]) continue;
        EditFace *f = &m->faces[i];
        for (j = 0; j < f->nv; j++) f->v[j] = dup[f->v[j]];
        editFaceComputeNormal(m, f);
    }

    free(dup); free(ea); free(eb); free(emat);
    return nsel;
}

#endif /* EDIT_OPS_H */
