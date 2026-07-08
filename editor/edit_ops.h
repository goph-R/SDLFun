#ifndef EDIT_OPS_H
#define EDIT_OPS_H

/* ---- Editor mesh operations (M4: extrude; polish: merge / recalc) ---------
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

/* ---- polish: merge-by-distance -------------------------------------------
 *
 * Weld every pair of verts within `eps` into one representative (the lowest
 * index), remap all faces onto the survivors, collapse any face that loses an
 * edge to the weld (quad -> tri, or drop it if it falls below a triangle), then
 * compact. Returns the number of verts removed.
 *
 * With the 1 cm snap, verts that *should* coincide are bit-identical, so a tiny
 * eps (half a snap cell) welds true duplicates and floating dust while leaving
 * any two intentionally-distinct verts (>= 1 cm apart) untouched. O(V^2) — fine
 * at editor scale, and the caller runs it as a one-shot menu op.
 * -------------------------------------------------------------------------- */
static int editMergeByDistance(EditMesh *m, float eps)
{
    int nv = m->numVerts, nf = m->numFaces, i, j, k;
    if (nv == 0) return 0;

    int *rep = (int *)malloc((nv > 0 ? nv : 1) * sizeof(int));
    for (i = 0; i < nv; i++) rep[i] = i;

    float e2 = eps * eps;
    for (i = 0; i < nv; i++) {
        if (rep[i] != i) continue;                 /* already welded away */
        for (j = i + 1; j < nv; j++) {
            if (rep[j] != j) continue;
            float dx = m->verts[i].pos.x - m->verts[j].pos.x;
            float dy = m->verts[i].pos.y - m->verts[j].pos.y;
            float dz = m->verts[i].pos.z - m->verts[j].pos.z;
            if (dx * dx + dy * dy + dz * dz <= e2) rep[j] = i;   /* j -> i */
        }
    }

    unsigned char *keepV = (unsigned char *)malloc(nv > 0 ? nv : 1);
    unsigned char *keepF = (unsigned char *)malloc(nf > 0 ? nf : 1);
    for (i = 0; i < nv; i++) keepV[i] = (rep[i] == i) ? 1 : 0;
    for (i = 0; i < nf; i++) keepF[i] = 1;

    /* Remap faces to representatives, dropping runs of coincident verts (a quad
       whose two verts welded becomes a tri; anything below 3 unique verts is a
       collapsed face and gets deleted). */
    for (i = 0; i < nf; i++) {
        EditFace *f = &m->faces[i];
        int out[4], c = 0;
        for (k = 0; k < f->nv; k++) {
            int vv = rep[f->v[k]];
            if (c == 0 || vv != out[c - 1]) out[c++] = vv;
        }
        if (c > 1 && out[c - 1] == out[0]) c--;     /* cyclic wrap duplicate */
        if (c < 3) { keepF[i] = 0; continue; }      /* collapsed -> delete */
        f->v[0] = out[0]; f->v[1] = out[1]; f->v[2] = out[2];
        f->v[3] = (c == 4) ? out[3] : -1;
        f->nv   = c;
    }

    editMeshCompact(m, keepV, keepF);
    for (i = 0; i < m->numFaces; i++) editFaceComputeNormal(m, &m->faces[i]);

    free(rep); free(keepV); free(keepF);
    return nv - m->numVerts;
}

/* ---- polish: recalculate consistent (outward) normals ---------------------
 *
 * Blender's "Recalculate Outside" (Shift+N). Flood-fills edge-adjacent faces
 * from a seed, flipping any face wound against its neighbour so all windings in
 * a connected shell agree; then, if the shell is *closed*, flips the whole shell
 * when its signed volume is negative so normals face out. Open shells (walls,
 * planes) are left seed-relative — there is no "outside" to point at, so we only
 * guarantee consistency, not a global sign. Returns the number of faces flipped.
 *
 * Adjacency is found from a snapshot directed-edge table; flipping never changes
 * an edge's *undirected* vertex pair, so the table stays a valid adjacency index
 * even as we flip, and each flip decision reads the neighbour's LIVE winding.
 * O(E^2) — a manual, one-shot menu op. Include after edit_mesh.h.
 * -------------------------------------------------------------------------- */
static int editRecalcNormalsConsistent(EditMesh *m)
{
    int nf = m->numFaces, i, j, k;
    if (nf == 0) return 0;

    int ne = 0;
    for (i = 0; i < nf; i++) ne += m->faces[i].nv;

    int *ea    = (int *)malloc((ne > 0 ? ne : 1) * sizeof(int));
    int *eb    = (int *)malloc((ne > 0 ? ne : 1) * sizeof(int));
    int *ef    = (int *)malloc((ne > 0 ? ne : 1) * sizeof(int));
    int *estart = (int *)malloc(nf * sizeof(int));   /* first edge of each face */
    int t = 0;
    for (i = 0; i < nf; i++) {
        EditFace *f = &m->faces[i];
        estart[i] = t;
        for (j = 0; j < f->nv; j++) {
            ea[t] = f->v[j];
            eb[t] = f->v[(j + 1) % f->nv];
            ef[t] = i;
            t++;
        }
    }

    /* how many table entries share each edge's undirected key (2 == manifold) */
    int *use = (int *)malloc((ne > 0 ? ne : 1) * sizeof(int));
    for (i = 0; i < ne; i++) {
        int a = ea[i], b = eb[i], c = 0;
        for (k = 0; k < ne; k++)
            if ((ea[k] == a && eb[k] == b) || (ea[k] == b && eb[k] == a)) c++;
        use[i] = c;
    }

    unsigned char *visited = (unsigned char *)malloc(nf);
    for (i = 0; i < nf; i++) visited[i] = 0;
    int *stack = (int *)malloc(nf * sizeof(int));
    int *comp  = (int *)malloc(nf * sizeof(int));
    int flips = 0;

    for (int seed = 0; seed < nf; seed++) {
        if (visited[seed]) continue;

        int sp = 0, nc = 0, closed = 1;
        stack[sp++] = seed; visited[seed] = 1;
        while (sp > 0) {
            int fi = stack[--sp];
            comp[nc++] = fi;
            EditFace *f = &m->faces[fi];
            for (j = 0; j < f->nv; j++) {
                if (use[estart[fi] + j] != 2) closed = 0;   /* boundary edge */
                int a = f->v[j], b = f->v[(j + 1) % f->nv];
                for (k = 0; k < ne; k++) {
                    int nb = ef[k];
                    if (nb == fi || visited[nb]) continue;
                    if (!((ea[k] == a && eb[k] == b) ||
                          (ea[k] == b && eb[k] == a))) continue;  /* not this edge */
                    /* neighbour shares undirected {a,b}; read its LIVE winding —
                       if it runs the edge the SAME way as fi, it's inconsistent. */
                    EditFace *g = &m->faces[nb];
                    int sameDir = 0, p;
                    for (p = 0; p < g->nv; p++)
                        if (g->v[p] == a && g->v[(p + 1) % g->nv] == b) { sameDir = 1; break; }
                    if (sameDir) { editFlipFace(m, nb); flips++; }
                    visited[nb] = 1; stack[sp++] = nb;
                    break;                                  /* one neighbour / edge */
                }
            }
        }

        if (!closed) continue;                              /* open: consistency only */

        double vol = 0.0;                                   /* signed shell volume */
        for (i = 0; i < nc; i++) {
            EditFace *f = &m->faces[comp[i]];
            Vec3 *v0 = &m->verts[f->v[0]].pos;
            for (j = 1; j + 1 < f->nv; j++) {               /* fan-triangulate */
                Vec3 *v1 = &m->verts[f->v[j]].pos;
                Vec3 *v2 = &m->verts[f->v[j + 1]].pos;
                double cx = (double)v1->y * v2->z - (double)v1->z * v2->y;
                double cy = (double)v1->z * v2->x - (double)v1->x * v2->z;
                double cz = (double)v1->x * v2->y - (double)v1->y * v2->x;
                vol += v0->x * cx + v0->y * cy + v0->z * cz;
            }
        }
        if (vol < 0.0)                                      /* wound inward -> flip all */
            for (i = 0; i < nc; i++) { editFlipFace(m, comp[i]); flips++; }
    }

    for (i = 0; i < nf; i++) editFaceComputeNormal(m, &m->faces[i]);

    free(ea); free(eb); free(ef); free(estart); free(use);
    free(visited); free(stack); free(comp);
    return flips;
}

#endif /* EDIT_OPS_H */
