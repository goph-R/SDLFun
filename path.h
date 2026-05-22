#ifndef PATH_H
#define PATH_H

/*
 * Platform path system.
 *
 * Designer drops `path_node` entities with a shared `group` and an
 * `order=N` key. At load time, pathTableBuild() harvests them into
 * PathGroup tables (sorted by order, segment lengths precomputed) and
 * resolves leader/sibling linkage for ENT_PLATFORM entities sharing the
 * same `group` as the leader's pathGroup target.
 *
 * Per-frame, updatePlatforms() (in main.cpp) walks leaders, advances
 * segT along the active segment, and translates+rotates the leader,
 * its siblings, and any player riding them. See docs/PLAN_PATH_PLATFORMS.md.
 *
 * Depends on obj_loader.h (Vec3) and entity.h (EntityList / ENT_PATH_NODE
 * / ENT_PLATFORM).
 */

#include <math.h>
#include <string.h>

#include "obj_loader.h"
#include "entity.h"

#define PATH_MAX_GROUPS 32
#define PATH_MAX_NODES  32

struct PathGroup {
    char  name[32];
    int   nodeEnts[PATH_MAX_NODES];   /* entity indices, sorted by pathNode.order asc */
    int   nodeCount;
    float segLen[PATH_MAX_NODES];     /* segLen[i] = |node[i+1] - node[i]|; last entry unused */
    float totalLen;
};

struct PathTable {
    PathGroup groups[PATH_MAX_GROUPS];
    int       count;
};

static PathGroup *pathTableFind(PathTable *pt, const char *name)
{
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < pt->count; i++) {
        if (strcmp(pt->groups[i].name, name) == 0) return &pt->groups[i];
    }
    return NULL;
}

/* Returns the world position of the Nth node in this group, looked up
   through the entity list (positions live there, not duplicated here). */
static Vec3 path_nodePos(const PathGroup *pg, const EntityList *el, int n)
{
    Vec3 r = { 0, 0, 0 };
    if (n < 0 || n >= pg->nodeCount) return r;
    const Entity *e = &el->entities[pg->nodeEnts[n]];
    r.x = e->posX; r.y = e->posY; r.z = e->posZ;
    return r;
}

/* Find-or-create a group by name. Returns NULL if table is full. */
static PathGroup *path_getOrCreate(PathTable *pt, const char *name)
{
    PathGroup *pg = pathTableFind(pt, name);
    if (pg) return pg;
    if (pt->count >= PATH_MAX_GROUPS) return NULL;
    pg = &pt->groups[pt->count++];
    memset(pg, 0, sizeof(*pg));
    strncpy(pg->name, name, 31);
    return pg;
}

/* Insertion-sort node indices within a group by pathNode.order (ascending). */
static void path_sortNodes(PathGroup *pg, const EntityList *el)
{
    for (int i = 1; i < pg->nodeCount; i++) {
        int cur = pg->nodeEnts[i];
        int curOrder = el->entities[cur].pathNode.order;
        int j = i - 1;
        while (j >= 0 && el->entities[pg->nodeEnts[j]].pathNode.order > curOrder) {
            pg->nodeEnts[j + 1] = pg->nodeEnts[j];
            j--;
        }
        pg->nodeEnts[j + 1] = cur;
    }
}

static void path_precomputeSegments(PathGroup *pg, const EntityList *el)
{
    pg->totalLen = 0.0f;
    for (int i = 0; i + 1 < pg->nodeCount; i++) {
        Vec3 a = path_nodePos(pg, el, i);
        Vec3 b = path_nodePos(pg, el, i + 1);
        float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
        float L = sqrtf(dx*dx + dy*dy + dz*dz);
        pg->segLen[i] = L;
        pg->totalLen += L;
    }
}

/* Linear-interpolate position along the path at (segIdx, segT). segT is
   clamped to [0,1] and segIdx to [0, nodeCount-2]. Returns node[0] if the
   group has only one node, or origin if empty. */
static Vec3 pathSample(const PathGroup *pg, const EntityList *el, int segIdx, float segT)
{
    Vec3 r = { 0, 0, 0 };
    if (pg->nodeCount == 0) return r;
    if (pg->nodeCount == 1) return path_nodePos(pg, el, 0);
    if (segIdx < 0) segIdx = 0;
    if (segIdx > pg->nodeCount - 2) segIdx = pg->nodeCount - 2;
    if (segT < 0.0f) segT = 0.0f;
    if (segT > 1.0f) segT = 1.0f;
    Vec3 a = path_nodePos(pg, el, segIdx);
    Vec3 b = path_nodePos(pg, el, segIdx + 1);
    r.x = a.x + (b.x - a.x) * segT;
    r.y = a.y + (b.y - a.y) * segT;
    r.z = a.z + (b.z - a.z) * segT;
    return r;
}

/* Advance the leader's (segIdx, segT, dir) by speed*dt along the path,
   handling segment boundaries and end-of-path (PATH_ONCE → finished,
   PATH_PING_PONG → reverse dir). Mutates the leader entity in place. */
static void pathAdvance(const PathGroup *pg, Entity *leader, float dt)
{
    if (!leader || pg->nodeCount < 2) return;
    if (leader->platform.finished) return;

    float distToMove = leader->platform.speed * dt;
    if (distToMove < 0.0f) distToMove = 0.0f;

    int   segIdx = leader->platform.segIdx;
    float segT   = leader->platform.segT;
    int   dir    = leader->platform.dir;
    if (dir == 0) dir = +1;

    int safety = 0;
    while (distToMove > 1e-6f && safety++ < 64) {
        float L = pg->segLen[segIdx];
        float remaining;
        if (L < 1e-6f) {
            /* Zero-length segment: hop straight to the next/prev node. */
            remaining = 0.0f;
        } else {
            remaining = (dir > 0) ? (1.0f - segT) * L : segT * L;
        }

        if (distToMove < remaining) {
            segT += (distToMove / L) * (float)dir;
            if (segT < 0.0f) segT = 0.0f;
            if (segT > 1.0f) segT = 1.0f;
            distToMove = 0.0f;
            break;
        }

        /* Consume the rest of this segment and step to the next. */
        distToMove -= remaining;

        if (dir > 0) {
            if (segIdx + 1 < pg->nodeCount - 1) {
                segIdx++;
                segT = 0.0f;
            } else {
                /* Past the final node. */
                segT = 1.0f;
                if (leader->platform.moveType == PATH_PING_PONG) {
                    dir = -1;
                } else {
                    leader->platform.finished = 1;
                    break;
                }
            }
        } else {
            if (segIdx > 0) {
                segIdx--;
                segT = 1.0f;
            } else {
                /* Past node 0 going backward. */
                segT = 0.0f;
                if (leader->platform.moveType == PATH_PING_PONG) {
                    dir = +1;
                } else {
                    leader->platform.finished = 1;
                    break;
                }
            }
        }
    }

    leader->platform.segIdx = segIdx;
    leader->platform.segT   = segT;
    leader->platform.dir    = dir;
}

/* Resolve leader/sibling linkage for ENT_PLATFORM entities. Called from
   pathTableBuild after node groups are built.

   - Pass A: the first ENT_PLATFORM in each Entity.group that has a non-empty
     pathGroup becomes the leader of that group. Snap its position to node 0
     of its path. Capture rotOffset = authored rotY.
   - Pass B: every non-leader platform with a group links to that group's
     leader. Capture (offX/Y/Z) in leader-local (unrotated) space using the
     leader's rotOffset as the reference frame. sibLocalAngle = sibling.rotY
     minus the leader's authored rotY.

   Stationary groups (no platform with pathGroup set) leave all members with
   isLeader=0 and no leader linkage — those entities just sit where authored. */
static void path_resolveLeaders(PathTable *pt, EntityList *el)
{
    /* Pass A: leaders. */
    for (int i = 0; i < el->count; i++) {
        Entity *e = &el->entities[i];
        if (!e->active || e->type != ENT_PLATFORM) continue;
        if (!e->platform.pathGroup[0]) continue;
        if (!e->group[0]) {
            conLogf("path: platform '%s' has path= but no group, ignoring path\n", e->name);
            continue;
        }

        /* Already a leader earlier in this entity-group? */
        int already = 0;
        for (int j = 0; j < i; j++) {
            Entity *p = &el->entities[j];
            if (!p->active || p->type != ENT_PLATFORM) continue;
            if (p->platform.isLeader && strcmp(p->group, e->group) == 0) { already = 1; break; }
        }
        if (already) {
            conLogf("path: platform '%s' wants to lead group '%s' but it already has a leader\n",
                    e->name, e->group);
            continue;
        }

        e->platform.isLeader  = 1;
        e->platform.leaderIdx = i;
        e->platform.segIdx    = 0;
        e->platform.segT      = 0.0f;
        if (e->platform.dir == 0) e->platform.dir = +1;
        e->platform.finished  = 0;
        e->platform.rotOffset = e->rotY;     /* captured before any snap */

        /* Snap to node 0 of its path group, if the path exists and is usable. */
        PathGroup *pg = pathTableFind(pt, e->platform.pathGroup);
        if (!pg) {
            conLogf("path: platform '%s' references missing path '%s'\n",
                    e->name, e->platform.pathGroup);
            continue;
        }
        if (pg->nodeCount < 2) {
            conLogf("path: platform '%s' path '%s' has %d node(s); needs >=2 to move\n",
                    e->name, e->platform.pathGroup, pg->nodeCount);
            continue;
        }
        Vec3 n0 = path_nodePos(pg, el, 0);
        e->posX = n0.x; e->posY = n0.y; e->posZ = n0.z;
    }

    /* Pass B: siblings. */
    for (int i = 0; i < el->count; i++) {
        Entity *e = &el->entities[i];
        if (!e->active || e->type != ENT_PLATFORM) continue;
        if (e->platform.isLeader) {
            /* Self-leader's own offset is zero (it IS the pivot). */
            e->platform.offX = 0; e->platform.offY = 0; e->platform.offZ = 0;
            e->platform.sibLocalAngle = 0;
            continue;
        }
        if (!e->group[0]) continue;

        /* Locate this entity-group's leader. */
        Entity *leader = NULL;
        int leaderIdx = -1;
        for (int j = 0; j < el->count; j++) {
            Entity *p = &el->entities[j];
            if (!p->active || p->type != ENT_PLATFORM) continue;
            if (!p->platform.isLeader) continue;
            if (strcmp(p->group, e->group) == 0) { leader = p; leaderIdx = j; break; }
        }
        if (!leader) continue;   /* sibling but no leader — stationary cluster, fine */

        e->platform.leaderIdx = leaderIdx;
        e->platform.sibLocalAngle = e->rotY - leader->platform.rotOffset;

        /* Capture offset in leader-local (unrotated) space using the leader's
           authored rotY as the reference frame. Inverse-rotate (wx, wz) by r0. */
        float r0 = leader->platform.rotOffset * (float)M_PI / 180.0f;
        float c = cosf(r0), s = sinf(r0);
        float wx = e->posX - leader->posX;
        float wz = e->posZ - leader->posZ;
        e->platform.offX =  c * wx - s * wz;
        e->platform.offZ =  s * wx + c * wz;
        e->platform.offY =  e->posY - leader->posY;
    }
}

/* Build the path table from an entity list. Harvests path_node entities
   into groups (sorted by order, segment lengths precomputed), then resolves
   leader/sibling linkage for ENT_PLATFORM entities. */
static void pathTableBuild(PathTable *pt, EntityList *el)
{
    memset(pt, 0, sizeof(*pt));

    for (int i = 0; i < el->count; i++) {
        Entity *e = &el->entities[i];
        if (!e->active || e->type != ENT_PATH_NODE) continue;
        if (!e->group[0]) {
            conLogf("path: path_node '%s' has no group, skipping\n", e->name);
            continue;
        }
        PathGroup *pg = path_getOrCreate(pt, e->group);
        if (!pg) {
            conLogf("path: PATH_MAX_GROUPS reached, dropping '%s'\n", e->group);
            continue;
        }
        if (pg->nodeCount >= PATH_MAX_NODES) {
            conLogf("path: group '%s' exceeds PATH_MAX_NODES, dropping nodes\n", pg->name);
            continue;
        }
        pg->nodeEnts[pg->nodeCount++] = i;
    }

    for (int g = 0; g < pt->count; g++) {
        path_sortNodes(&pt->groups[g], el);
        path_precomputeSegments(&pt->groups[g], el);
        conLogf("path: group '%s' has %d nodes, totalLen=%.2f\n",
                pt->groups[g].name, pt->groups[g].nodeCount, pt->groups[g].totalLen);
    }

    path_resolveLeaders(pt, el);
}

/* Wireframe overlay for paths and leader headings. Cyan polylines between
   consecutive nodes per group, small magenta octahedra at each node, and a
   magenta forward-arrow at each leader's current position pointing along
   its current yaw. Called from the collider-debug toggle (key B). */
static void pathDebugRender(const PathTable *pt, const EntityList *el)
{
    if (!pt || pt->count == 0) return;

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);

    /* Path polylines — cyan. */
    glColor3f(0.2f, 0.9f, 0.95f);
    glBegin(GL_LINES);
    for (int g = 0; g < pt->count; g++) {
        const PathGroup *pg = &pt->groups[g];
        for (int i = 0; i + 1 < pg->nodeCount; i++) {
            Vec3 a = path_nodePos(pg, el, i);
            Vec3 b = path_nodePos(pg, el, i + 1);
            glVertex3f(a.x, a.y, a.z);
            glVertex3f(b.x, b.y, b.z);
        }
    }
    glEnd();

    /* Node markers — small magenta octahedra. */
    const float s = 0.10f;
    glColor3f(1.0f, 0.3f, 0.9f);
    glBegin(GL_TRIANGLES);
    for (int g = 0; g < pt->count; g++) {
        const PathGroup *pg = &pt->groups[g];
        for (int i = 0; i < pg->nodeCount; i++) {
            Vec3 c = path_nodePos(pg, el, i);
            float xs[4] = { c.x + s, c.x, c.x - s, c.x };
            float zs[4] = { c.z, c.z + s, c.z, c.z - s };
            for (int k = 0; k < 4; k++) {
                int k2 = (k + 1) & 3;
                glVertex3f(c.x, c.y + s, c.z);
                glVertex3f(xs[k],  c.y, zs[k]);
                glVertex3f(xs[k2], c.y, zs[k2]);
                glVertex3f(c.x, c.y - s, c.z);
                glVertex3f(xs[k2], c.y, zs[k2]);
                glVertex3f(xs[k],  c.y, zs[k]);
            }
        }
    }
    glEnd();

    /* Leader heading arrow — magenta line from leader position 1m forward
       along its current yaw. Makes face_path failures obvious at a glance. */
    glColor3f(1.0f, 0.2f, 0.8f);
    glLineWidth(3.0f);
    glBegin(GL_LINES);
    for (int i = 0; i < el->count; i++) {
        const Entity *e = &el->entities[i];
        if (!e->active || e->type != ENT_PLATFORM) continue;
        if (!e->platform.isLeader) continue;
        float rad = e->rotY * (float)M_PI / 180.0f;
        float fx = sinf(rad), fz = cosf(rad);
        glVertex3f(e->posX, e->posY + 0.5f, e->posZ);
        glVertex3f(e->posX + fx, e->posY + 0.5f, e->posZ + fz);
    }
    glEnd();
    glLineWidth(1.0f);

    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);
}

#endif
