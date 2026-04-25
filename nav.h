#ifndef NAV_H
#define NAV_H

/*
 * Waypoint nav graph + A* pathfinder (HL1 info_node-style).
 *
 * Designer drops `waypoint` entities in the .ent file. At load time,
 * navInit() harvests their positions and auto-connects any pair that
 * has mutual line-of-sight (raycast at eye-ish height). At runtime,
 * navFindPath() runs A* over the graph to produce a list of node
 * indices to walk through.
 *
 * Static storage — 64 nodes is the scale we target (rooms + corridors
 * + vents). Adjacency is a bitset so a full graph fits cheaply and
 * neighbour iteration is a 32-bit popcnt walk.
 *
 * Depends on obj_loader.h (Vec3), entity.h (EntityList / ENT_WAYPOINT),
 * and physics.h (physRaycast).
 *
 * Phase 1 of docs/plan-enemies.md.
 */

#include <math.h>
#include <string.h>
#include <float.h>

#include "obj_loader.h"
#include "entity.h"
#include "physics.h"

#define NAV_MAX_NODES   64
#define NAV_EDGE_WORDS  ((NAV_MAX_NODES + 31) / 32)
#define NAV_EYE_OFFSET  0.5f    /* raycast height above the waypoint for LOS */

struct NavGraph {
    int  numNodes;
    Vec3 nodes[NAV_MAX_NODES];
    unsigned int edges[NAV_MAX_NODES][NAV_EDGE_WORDS];
    int  numEdges;  /* total directed edges, for debug logging */
};

/* ---- bitset helpers ---- */

static void nav_bitSet(unsigned int *bits, int i) { bits[i >> 5] |= (1u << (i & 31)); }
static void nav_bitClear(unsigned int *bits, int i) { bits[i >> 5] &= ~(1u << (i & 31)); }
static int  nav_bitGet(const unsigned int *bits, int i) { return (bits[i >> 5] >> (i & 31)) & 1u; }

static int nav_hasEdge(const NavGraph *g, int a, int b)
{
    return nav_bitGet(g->edges[a], b);
}

static float nav_dist(Vec3 a, Vec3 b)
{
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

/* Line-of-sight raycast between two points, at NAV_EYE_OFFSET above each.
   Returns 1 if the path is clear. Uses the slightly-under-distance max so
   we don't self-hit geometry tangent to the endpoint. */
static int nav_los(PhysWorld *pw, Vec3 a, Vec3 b)
{
    Vec3 from = { a.x, a.y + NAV_EYE_OFFSET, a.z };
    Vec3 to   = { b.x, b.y + NAV_EYE_OFFSET, b.z };
    float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
    if (dist < 1e-4f) return 1;
    float invD = 1.0f / dist;
    Vec3 dir = { dx * invD, dy * invD, dz * invD };
    int hit = physRaycast(pw, from, dir, dist - 0.05f, NULL);
    return !hit;
}

/* ---- lifecycle ---- */

static void navInit(NavGraph *g, EntityList *el, PhysWorld *pw)
{
    memset(g, 0, sizeof(*g));

    /* Harvest waypoint positions. */
    for (int i = 0; i < el->count && g->numNodes < NAV_MAX_NODES; i++) {
        Entity *e = &el->entities[i];
        if (!e->active || e->type != ENT_WAYPOINT) continue;
        g->nodes[g->numNodes].x = e->posX;
        g->nodes[g->numNodes].y = e->posY;
        g->nodes[g->numNodes].z = e->posZ;
        g->numNodes++;
    }

    if (g->numNodes == 0) {
        conLogf("nav: no waypoints placed\n");
        return;
    }

    /* Pairwise LOS for symmetric undirected edges. */
    int edgeCount = 0;
    for (int i = 0; i < g->numNodes; i++) {
        for (int j = i + 1; j < g->numNodes; j++) {
            if (nav_los(pw, g->nodes[i], g->nodes[j])) {
                nav_bitSet(g->edges[i], j);
                nav_bitSet(g->edges[j], i);
                edgeCount += 2;
            }
        }
    }
    g->numEdges = edgeCount;

    conLogf("nav: %d nodes, %d directed edges (%d undirected) built\n",
           g->numNodes, edgeCount, edgeCount / 2);
}

/* Nearest node to `p` with LOS from `p`. Falls back to plain nearest if
   no visible node is found (lets A* still produce *something* rather
   than failing hard when the caller is inside a prop). */
static int nav_nearestVisible(const NavGraph *g, PhysWorld *pw, Vec3 p)
{
    int bestVis = -1, bestAny = -1;
    float bestVisD = FLT_MAX, bestAnyD = FLT_MAX;
    for (int i = 0; i < g->numNodes; i++) {
        float d = nav_dist(p, g->nodes[i]);
        if (d < bestAnyD) { bestAnyD = d; bestAny = i; }
        if (d < bestVisD && nav_los(pw, p, g->nodes[i])) {
            bestVisD = d; bestVis = i;
        }
    }
    return (bestVis >= 0) ? bestVis : bestAny;
}

/* A* over the graph. Writes node indices (entry ... exit inclusive) into
   outNodes and returns the count, or 0 on failure. maxLen caps the output
   length; if the true path is longer, returns 0. */
static int navFindPath(NavGraph *g, PhysWorld *pw, Vec3 from, Vec3 to,
                       int *outNodes, int maxLen)
{
    if (g->numNodes == 0) return 0;

    int start = nav_nearestVisible(g, pw, from);
    int goal  = nav_nearestVisible(g, pw, to);
    if (start < 0 || goal < 0) return 0;
    if (start == goal) {
        if (maxLen < 1) return 0;
        outNodes[0] = start;
        return 1;
    }

    float gScore[NAV_MAX_NODES];
    float fScore[NAV_MAX_NODES];
    int   cameFrom[NAV_MAX_NODES];
    unsigned int openSet[NAV_EDGE_WORDS];
    unsigned int closedSet[NAV_EDGE_WORDS];

    for (int i = 0; i < g->numNodes; i++) {
        gScore[i] = FLT_MAX;
        fScore[i] = FLT_MAX;
        cameFrom[i] = -1;
    }
    memset(openSet, 0, sizeof(openSet));
    memset(closedSet, 0, sizeof(closedSet));

    gScore[start] = 0.0f;
    fScore[start] = nav_dist(g->nodes[start], g->nodes[goal]);
    nav_bitSet(openSet, start);

    while (1) {
        /* Pick the open-set node with lowest fScore (linear scan — fine
           at our scale, no need for a heap). */
        int current = -1;
        float bestF = FLT_MAX;
        for (int i = 0; i < g->numNodes; i++) {
            if (!nav_bitGet(openSet, i)) continue;
            if (fScore[i] < bestF) { bestF = fScore[i]; current = i; }
        }
        if (current < 0) return 0; /* open set empty, no path */

        if (current == goal) {
            /* Reconstruct path into a temp, then reverse into outNodes. */
            int tmp[NAV_MAX_NODES];
            int len = 0;
            for (int cur = goal; cur >= 0 && len < NAV_MAX_NODES; cur = cameFrom[cur]) {
                tmp[len++] = cur;
            }
            if (len > maxLen) return 0;
            for (int i = 0; i < len; i++) outNodes[i] = tmp[len - 1 - i];
            return len;
        }

        nav_bitClear(openSet, current);
        nav_bitSet(closedSet, current);

        for (int n = 0; n < g->numNodes; n++) {
            if (!nav_hasEdge(g, current, n)) continue;
            if (nav_bitGet(closedSet, n)) continue;
            float tentative = gScore[current]
                            + nav_dist(g->nodes[current], g->nodes[n]);
            if (tentative >= gScore[n]) continue;
            cameFrom[n] = current;
            gScore[n] = tentative;
            fScore[n] = tentative + nav_dist(g->nodes[n], g->nodes[goal]);
            nav_bitSet(openSet, n);
        }
    }
}

/* ---- debug render ----
   Call between a fresh glLoadIdentity() + camera transform and before UI.
   Disables texturing and lighting, draws nodes as small axis-aligned
   diamonds and edges as dim white lines. Leaves GL state roughly as it
   found it so the caller can keep rendering. */
static void navDebugRender(const NavGraph *g)
{
    if (g->numNodes == 0) return;

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);

    /* Edges — dim white lines. Walk the upper triangle only so we don't
       draw each edge twice. */
    glColor3f(0.55f, 0.55f, 0.65f);
    glBegin(GL_LINES);
    for (int i = 0; i < g->numNodes; i++) {
        for (int j = i + 1; j < g->numNodes; j++) {
            if (!nav_hasEdge(g, i, j)) continue;
            Vec3 a = g->nodes[i], b = g->nodes[j];
            glVertex3f(a.x, a.y + NAV_EYE_OFFSET, a.z);
            glVertex3f(b.x, b.y + NAV_EYE_OFFSET, b.z);
        }
    }
    glEnd();

    /* Nodes — small cyan octahedrons (6 verts) so they read clearly
       against floor/wall without needing a texture. */
    const float s = 0.12f;
    glColor3f(0.2f, 0.95f, 0.9f);
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < g->numNodes; i++) {
        Vec3 c = g->nodes[i];
        c.y += NAV_EYE_OFFSET;
        /* +Y top, -Y bottom, ring on XZ at y=c.y */
        float xs[4] = { c.x + s, c.x, c.x - s, c.x };
        float zs[4] = { c.z, c.z + s, c.z, c.z - s };
        for (int k = 0; k < 4; k++) {
            int k2 = (k + 1) & 3;
            /* top */
            glVertex3f(c.x, c.y + s, c.z);
            glVertex3f(xs[k],  c.y,      zs[k]);
            glVertex3f(xs[k2], c.y,      zs[k2]);
            /* bottom */
            glVertex3f(c.x, c.y - s, c.z);
            glVertex3f(xs[k2], c.y,      zs[k2]);
            glVertex3f(xs[k],  c.y,      zs[k]);
        }
    }
    glEnd();

    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);
}

#endif /* NAV_H */
