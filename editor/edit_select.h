#ifndef EDIT_SELECT_H
#define EDIT_SELECT_H

/* ---- Editor selection state (M1) ------------------------------------------
 *
 * Three selection modes (vertex / edge / face), à la Blender's 1/2/3. Each mode
 * keeps its own boolean selection array; conversion between modes (select a
 * face -> its verts) is a later nicety. Selection is what picking writes and
 * what the overlay highlights.
 *
 * Edges aren't stored in the mesh (an indexed face set has none) — they're
 * DERIVED here: unique undirected (a,b) pairs across all face edges, rebuilt
 * whenever topology changes. The dedup is O(edges^2), fine at editor scale and
 * only run on a rebuild; revisit with a hash if huge meshes ever need it.
 *
 * Pure data: no GL, no FLTK. Include after edit_mesh.h.
 * -------------------------------------------------------------------------- */

#include <cstdlib>
#include <cstring>
#include "edit_mesh.h"

typedef enum { SEL_VERT = 0, SEL_EDGE = 1, SEL_FACE = 2 } EditSelMode;

typedef struct { int a, b; } EditEdge;   /* a < b (vertex indices) */

typedef struct {
    EditSelMode    mode;
    unsigned char *vertSel;   /* size numVerts */
    unsigned char *faceSel;   /* size numFaces */
    EditEdge      *edges;     /* derived */
    unsigned char *edgeSel;   /* size numEdges */
    int            numEdges;
    int            numVerts;  /* sizes the arrays were built for */
    int            numFaces;
} EditSelection;

/* Build the unique undirected edge list from a mesh's faces. */
static void editEdgesBuild(EditMesh *m, EditEdge **outE, int *outN)
{
    int capE = 0, i, k;
    for (i = 0; i < m->numFaces; i++) capE += m->faces[i].nv;
    EditEdge *e = (EditEdge *)malloc((capE > 0 ? capE : 1) * sizeof(EditEdge));
    int n = 0;

    for (i = 0; i < m->numFaces; i++) {
        EditFace *f = &m->faces[i];
        for (k = 0; k < f->nv; k++) {
            int a = f->v[k];
            int b = f->v[(k + 1) % f->nv];
            if (a > b) { int t = a; a = b; b = t; }
            int found = 0, j;
            for (j = 0; j < n; j++)
                if (e[j].a == a && e[j].b == b) { found = 1; break; }
            if (!found) { e[n].a = a; e[n].b = b; n++; }
        }
    }
    *outE = e;
    *outN = n;
}

/* Allocate zeroed selection arrays sized to the mesh, derive edges, mode=VERT.
   Call after the mesh is built; re-run editSelRebuild after topology edits. */
static void editSelInit(EditSelection *s, EditMesh *m)
{
    s->mode     = SEL_VERT;
    s->numVerts = m->numVerts;
    s->numFaces = m->numFaces;
    s->vertSel  = (unsigned char *)calloc(m->numVerts > 0 ? m->numVerts : 1, 1);
    s->faceSel  = (unsigned char *)calloc(m->numFaces > 0 ? m->numFaces : 1, 1);
    editEdgesBuild(m, &s->edges, &s->numEdges);
    s->edgeSel  = (unsigned char *)calloc(s->numEdges > 0 ? s->numEdges : 1, 1);
}

static void editSelFree(EditSelection *s)
{
    free(s->vertSel); free(s->faceSel); free(s->edges); free(s->edgeSel);
    s->vertSel = s->faceSel = s->edgeSel = NULL;
    s->edges = NULL;
    s->numEdges = s->numVerts = s->numFaces = 0;
}

/* Clear every selection array (all three modes). */
static void editSelClearAll(EditSelection *s)
{
    memset(s->vertSel, 0, s->numVerts);
    memset(s->faceSel, 0, s->numFaces);
    memset(s->edgeSel, 0, s->numEdges);
}

/* Clear only the active mode's selection. */
static void editSelClearActive(EditSelection *s)
{
    if (s->mode == SEL_VERT) memset(s->vertSel, 0, s->numVerts);
    else if (s->mode == SEL_EDGE) memset(s->edgeSel, 0, s->numEdges);
    else memset(s->faceSel, 0, s->numFaces);
}

#endif /* EDIT_SELECT_H */
