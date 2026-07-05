#ifndef EDIT_PICK_H
#define EDIT_PICK_H

/* ---- Editor picking (M1) --------------------------------------------------
 *
 * Screen-space vertex/edge picking and ray-triangle face picking, driven by a
 * camera BASIS (eye + right/up/forward + fov/aspect/viewport) rather than GL
 * matrices — so it needs no gluProject/gluUnProject and no matrix inverse, in
 * keeping with the engine's no-GLU, manual-matrix approach. The caller fills a
 * PickCam from the editor camera each time it picks.
 *
 * Pixel convention: mouse (mx,my) is viewport-local, origin top-left, y down —
 * exactly what projection here produces, so they compare directly.
 *
 * Pure math: no GL, no FLTK. Include after edit_mesh.h.
 * -------------------------------------------------------------------------- */

#include <cmath>
#include "edit_mesh.h"

typedef struct {
    Vec3  eye, right, up, forward;   /* orthonormal camera basis */
    float tanHalfFov;                /* tan(fovY / 2) */
    float aspect;                    /* viewport w / h */
    int   vpW, vpH;                  /* viewport pixels */
} PickCam;

/* -- tiny vec helpers (edit-prefixed to avoid clashing with engine_math) -- */
static Vec3 editV3(float x, float y, float z){ Vec3 v; v.x=x; v.y=y; v.z=z; return v; }
static Vec3 editV3Sub(Vec3 a, Vec3 b){ return editV3(a.x-b.x, a.y-b.y, a.z-b.z); }
static Vec3 editV3Add(Vec3 a, Vec3 b){ return editV3(a.x+b.x, a.y+b.y, a.z+b.z); }
static Vec3 editV3Scale(Vec3 a, float s){ return editV3(a.x*s, a.y*s, a.z*s); }
static float editV3Dot(Vec3 a, Vec3 b){ return a.x*b.x + a.y*b.y + a.z*b.z; }
static Vec3 editV3Cross(Vec3 a, Vec3 b)
{ return editV3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x); }
static Vec3 editV3Norm(Vec3 a)
{
    float l = sqrtf(editV3Dot(a, a));
    if (l > 1e-8f) return editV3Scale(a, 1.0f / l);
    return a;
}

/* Project a world point to viewport pixels. Returns 1 if in front of the eye
   (and writes sx,sy); 0 if behind (sx,sy untouched). */
static int editProject(const PickCam *c, Vec3 p, float *sx, float *sy)
{
    Vec3 rel = editV3Sub(p, c->eye);
    float zc = editV3Dot(rel, c->forward);
    if (zc <= 1e-4f) return 0;
    float xc = editV3Dot(rel, c->right);
    float yc = editV3Dot(rel, c->up);
    float ndcx = (xc / zc) / (c->tanHalfFov * c->aspect);
    float ndcy = (yc / zc) / c->tanHalfFov;
    *sx = (ndcx * 0.5f + 0.5f) * (float)c->vpW;
    *sy = (1.0f - (ndcy * 0.5f + 0.5f)) * (float)c->vpH;
    return 1;
}

/* World-space ray through a viewport pixel (origin = eye). */
static void editMakeRay(const PickCam *c, float mx, float my, Vec3 *o, Vec3 *dir)
{
    float ndcx = (2.0f * mx / (float)c->vpW) - 1.0f;
    float ndcy = 1.0f - (2.0f * my / (float)c->vpH);
    Vec3 d = c->forward;
    d = editV3Add(d, editV3Scale(c->right, ndcx * c->tanHalfFov * c->aspect));
    d = editV3Add(d, editV3Scale(c->up,    ndcy * c->tanHalfFov));
    *o = c->eye;
    *dir = editV3Norm(d);
}

/* Nearest vertex within radiusPx of the mouse, or -1. */
static int editPickVertex(const PickCam *c, EditMesh *m,
                          float mx, float my, float radiusPx)
{
    int best = -1;
    float bestD2 = radiusPx * radiusPx;
    int i;
    for (i = 0; i < m->numVerts; i++) {
        float sx, sy;
        if (!editProject(c, m->verts[i].pos, &sx, &sy)) continue;
        float dx = sx - mx, dy = sy - my;
        float d2 = dx * dx + dy * dy;
        if (d2 <= bestD2) { bestD2 = d2; best = i; }
    }
    return best;
}

/* Squared distance from point (px,py) to segment (ax,ay)-(bx,by), in pixels. */
static float editSegDist2(float px, float py, float ax, float ay,
                          float bx, float by)
{
    float vx = bx - ax, vy = by - ay;
    float wx = px - ax, wy = py - ay;
    float len2 = vx * vx + vy * vy;
    float t = (len2 > 1e-6f) ? (wx * vx + wy * vy) / len2 : 0.0f;
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    float cx = ax + t * vx, cy = ay + t * vy;
    float dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy;
}

/* Nearest edge within radiusPx of the mouse, or -1. */
static int editPickEdge(const PickCam *c, EditMesh *m, EditEdge *edges,
                        int numEdges, float mx, float my, float radiusPx)
{
    int best = -1;
    float bestD2 = radiusPx * radiusPx;
    int i;
    for (i = 0; i < numEdges; i++) {
        float ax, ay, bx, by;
        if (!editProject(c, m->verts[edges[i].a].pos, &ax, &ay)) continue;
        if (!editProject(c, m->verts[edges[i].b].pos, &bx, &by)) continue;
        float d2 = editSegDist2(mx, my, ax, ay, bx, by);
        if (d2 <= bestD2) { bestD2 = d2; best = i; }
    }
    return best;
}

/* Möller–Trumbore ray/triangle. Writes hit distance t (>0) on success. */
static int editRayTri(Vec3 o, Vec3 d, Vec3 a, Vec3 b, Vec3 cc, float *tOut)
{
    Vec3 e1 = editV3Sub(b, a), e2 = editV3Sub(cc, a);
    Vec3 p = editV3Cross(d, e2);
    float det = editV3Dot(e1, p);
    if (det > -1e-6f && det < 1e-6f) return 0;
    float inv = 1.0f / det;
    Vec3 tv = editV3Sub(o, a);
    float u = editV3Dot(tv, p) * inv;
    if (u < 0.0f || u > 1.0f) return 0;
    Vec3 q = editV3Cross(tv, e1);
    float v = editV3Dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return 0;
    float t = editV3Dot(e2, q) * inv;
    if (t <= 1e-4f) return 0;
    *tOut = t;
    return 1;
}

/* Nearest face hit by the mouse ray, or -1. Tests each face's fan triangles. */
static int editPickFace(const PickCam *c, EditMesh *m, float mx, float my)
{
    Vec3 o, d;
    editMakeRay(c, mx, my, &o, &d);
    int best = -1;
    float bestT = 1e30f;
    int i;
    for (i = 0; i < m->numFaces; i++) {
        EditFace *f = &m->faces[i];
        Vec3 v0 = m->verts[f->v[0]].pos;
        Vec3 v1 = m->verts[f->v[1]].pos;
        Vec3 v2 = m->verts[f->v[2]].pos;
        float t;
        if (editRayTri(o, d, v0, v1, v2, &t) && t < bestT) { bestT = t; best = i; }
        if (f->nv == 4) {
            Vec3 v3 = m->verts[f->v[3]].pos;
            if (editRayTri(o, d, v0, v2, v3, &t) && t < bestT) { bestT = t; best = i; }
        }
    }
    return best;
}

#endif /* EDIT_PICK_H */
