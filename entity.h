#ifndef ENTITY_H
#define ENTITY_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <math.h>

#include "obj_loader.h"
#include "texture.h"
#include "iqm.h"
#include "asset_registry.h"

#define MAX_ENTITIES 256

/* ---- Entity types ---- */

enum EntityType {
    ENT_NONE = 0,
    ENT_PLAYER,
    ENT_DECORATION,
    ENT_ITEM,
    ENT_ENEMY,
    ENT_PLATFORM,
    ENT_SWITCH,
    ENT_TRIGGER,
    ENT_DOOR,
    ENT_WAYPOINT,  /* Nav node; position-only, no mesh/physics. */
    ENT_PATH_NODE, /* Platform path waypoint; grouped + ordered, position-only. */
    ENT_LIGHT      /* Sphere light; position-only + color/intensity/radius. The
                      game runtime ignores it (like a waypoint); authored in the
                      editor and consumed by the Blender lightmap bake. */
};

/* Path motion modes — stored as int in Entity.platform.moveType to keep
   the union POD-trivial across Dev-C++ / GCC 3.4. */
enum PathMoveType {
    PATH_ONCE      = 0,
    PATH_PING_PONG = 1
};

/* ---- Entity ---- */

struct Entity {
    int active;
    EntityType type;
    char name[32];
    char group[32];

    /* Transform */
    float posX, posY, posZ;
    float rotY;
    float scale;

    /* Authored asset names (the mesh=/tex=/iqm=/anim= tokens, verbatim). The
       loader resolves these into the loaded mesh/texture below and would
       otherwise discard them; kept so the editor can show them in the property
       form and re-emit them on .ent save. Empty = unset. */
    char meshName[32];
    char texName[32];
    char iqmName[32];
    char animName[32];

    /* Visual: static mesh (OBJ) */
    int hasMesh;
    ObjMesh mesh;
    GLuint diffuseTex;
    int isStatic;       /* baked into lightmap, no physics update */
    int flipCull;       /* 1 = use GL_FRONT culling (flipped winding) */

    /* Collision: 0 = none, 1 = static box (AABB from mesh verts). Held as
       void* so this header doesn't need Bullet. Populated by main after
       the physics world exists. */
    int collide;
    void *physBody;

    /* Visual: animated model (IQM) */
    int hasAnim;
    IqmModel iqmModel;
    int currentAnim;
    float animTime;
    float animSpeed;

    /* Type-specific data */
    union {
        struct {
            int itemType;   /* 0=health, 1=ammo, 2=key */
            int picked;
        } item;

        struct {
            int health;
            int state;      /* 0=idle, 1=patrol, 2=chase, 3=attack, 4=dead */
            float speed;
            float sightRange;
        } enemy;

        struct {
            char pathGroup[32];   /* name of path_node group this platform follows */
            int  moveType;        /* PathMoveType: 0=PATH_ONCE, 1=PATH_PING_PONG */
            float speed;          /* m/s along path */
            int  enabled;         /* 0=motion paused (collider stays), 1=running */
            int  facePath;        /* 1=yaw aligns to segment heading, 0=keep authored rotY */
            float rotOffset;      /* authored rotY at load, added to path-derived yaw */

            /* Leader/sibling linkage resolved at gameInit (see path.h). */
            int  isLeader;
            int  leaderIdx;       /* entity index of leader (== self if leader) */

            /* Leader-only motion state. */
            int  segIdx;          /* 0 = between node 0 and node 1 */
            float segT;           /* 0..1 along current segment */
            int  dir;             /* +1 forward, -1 reverse */
            int  finished;        /* PATH_ONCE reached the end */

            /* Sibling offset/yaw captured at gameInit, in leader-local
               (unrotated) space. Unused on the leader. */
            float offX, offY, offZ;
            float sibLocalAngle;  /* sibling.rotY - leader.initialRotY */

            /* Local-space AABB center (post-scale, pre-rotation) so the
               per-tick collider transform can be recomputed cheaply, like
               door.lcx/lcy/lcz. Populated by the collider-build loop. */
            float lcx, lcy, lcz;
        } platform;

        struct {
            int state;      /* 0=off, 1=on */
            char target[32];
        } sw;

        struct {
            float sizeX, sizeY, sizeZ;
            char target[32];
            int once;
            int triggered;
        } trigger;

        struct {
            int motion;        /* 0 = slide, 1 = rotate (around Y) */
            int axis;          /* 0 = X, 1 = Y, 2 = Z (slide only; rotate is Y) */
            float amount;      /* meters (slide) or degrees (rotate) */
            float speed;       /* m/s (slide) or deg/s (rotate) */
            int state;         /* 0 = closed, 1 = opening, 2 = open, 3 = closing */
            float progress;    /* 0..1, 0 = closed, 1 = fully open */
            float closedX, closedY, closedZ;
            float closedRotY;
            /* Cached local-space AABB center (post-scale, pre-rotation) so the
               door's per-tick collider transform can be recomputed cheaply. */
            float lcx, lcy, lcz;
            /* auto_close: seconds to stay open before auto-closing. 0 = stay
               open until re-activated (manual toggle, current switch-pair feel). */
            float autoCloseTime;
            float openTimer;
        } door;

        struct {
            int order;          /* sort key within the path group */
        } pathNode;

        struct {
            float r, g, b;      /* color, 0..1 */
            float intensity;    /* scalar multiplier */
            float radius;       /* sphere radius in metres */
        } light;
    };
};

/* ---- Entity List ---- */

struct EntityList {
    Entity entities[MAX_ENTITIES];
    int count;
    int playerIndex;
};

static void entListInit(EntityList *el)
{
    memset(el, 0, sizeof(EntityList));
    el->playerIndex = -1;
}

static int entCreate(EntityList *el, EntityType type)
{
    if (el->count >= MAX_ENTITIES) return -1;
    int idx = el->count++;
    Entity *e = &el->entities[idx];
    memset(e, 0, sizeof(Entity));
    e->active = 1;
    e->type = type;
    e->scale = 1.0f;
    e->animSpeed = 1.0f;
    if (type == ENT_PLAYER) el->playerIndex = idx;
    return idx;
}

static Entity *entFindByName(EntityList *el, const char *name)
{
    for (int i = 0; i < el->count; i++) {
        if (el->entities[i].active && strcmp(el->entities[i].name, name) == 0)
            return &el->entities[i];
    }
    return NULL;
}

/* Activate all entities matching a name or group */
static void entActivate(EntityList *el, const char *target)
{
    for (int i = 0; i < el->count; i++) {
        Entity *e = &el->entities[i];
        if (!e->active) continue;
        if (strcmp(e->name, target) == 0 || strcmp(e->group, target) == 0) {
            switch (e->type) {
            case ENT_PLATFORM: {
                /* Only the leader carries motion state. entActivate cascades
                   by group, so without this guard a target=lift1 cascade
                   would toggle leader.enabled once per group member. */
                if (!e->platform.isLeader) break;
                if (e->platform.finished) {
                    /* PATH_ONCE replay: reset to start. */
                    e->platform.segIdx = 0;
                    e->platform.segT   = 0.0f;
                    e->platform.dir    = +1;
                    e->platform.finished = 0;
                }
                e->platform.enabled = !e->platform.enabled;
                break;
            }
            case ENT_SWITCH:
                e->sw.state = !e->sw.state;
                /* Cascade: activate the switch's own target */
                if (e->sw.target[0])
                    entActivate(el, e->sw.target);
                break;
            case ENT_DOOR:
                /* Toggle: closed starts opening, open starts closing.
                   Mid-animation activations ignored (simplest sensible behavior). */
                if (e->door.state == 0) e->door.state = 1;
                else if (e->door.state == 2) e->door.state = 3;
                break;
            default:
                break;
            }
        }
    }
}

/* ---- Parse key=value from a token ---- */

static int entParseKV(const char *token, char *key, char *value)
{
    const char *eq = strchr(token, '=');
    if (!eq) return 0;
    int keyLen = (int)(eq - token);
    if (keyLen > 63) keyLen = 63;
    memcpy(key, token, keyLen);
    key[keyLen] = '\0';
    strncpy(value, eq + 1, 63);
    value[63] = '\0';
    return 1;
}

/* ---- Load entities from .ent file ---- */

static int entLoadFile(EntityList *el, const char *filename, TexCache *cache,
                       const AssetRegistry *reg)
{
    FILE *f = fopen(filename, "r");
    if (!f) {
        conLogf("entity: no .ent file (%s), using defaults\n", filename);
        return 0;
    }

    /* mesh=/tex=/iqm= values are resolved through the asset registry first
       (short logical names populated from assets.lua); unknown tokens are
       used verbatim as paths relative to the repo root. */

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\0') continue;

        /* Tokenize: type name group posX posY posZ rotY [key=value ...] */
        char *tokens[32];
        int count = 0;
        char *tok = strtok(line, " \t");
        while (tok && count < 32) {
            tokens[count++] = tok;
            tok = strtok(NULL, " \t");
        }
        if (count < 7) continue; /* need at least type name group x y z rotY */

        /* Determine entity type */
        EntityType type = ENT_NONE;
        if (strcmp(tokens[0], "player") == 0) type = ENT_PLAYER;
        else if (strcmp(tokens[0], "decoration") == 0) type = ENT_DECORATION;
        else if (strcmp(tokens[0], "item") == 0) type = ENT_ITEM;
        else if (strcmp(tokens[0], "enemy") == 0) type = ENT_ENEMY;
        else if (strcmp(tokens[0], "platform") == 0) type = ENT_PLATFORM;
        else if (strcmp(tokens[0], "switch") == 0) type = ENT_SWITCH;
        else if (strcmp(tokens[0], "trigger") == 0) type = ENT_TRIGGER;
        else if (strcmp(tokens[0], "door") == 0) type = ENT_DOOR;
        else if (strcmp(tokens[0], "waypoint") == 0) type = ENT_WAYPOINT;
        else if (strcmp(tokens[0], "path_node") == 0) type = ENT_PATH_NODE;
        else if (strcmp(tokens[0], "light") == 0) type = ENT_LIGHT;
        else { conLogf("entity: unknown type '%s'\n", tokens[0]); continue; }

        int idx = entCreate(el, type);
        if (idx < 0) { conLogf("entity: max entities reached\n"); break; }
        Entity *e = &el->entities[idx];

        /* Per-type defaults that need to land before the key=value loop so
           an absent key keeps a sensible default. entCreate memsets, which
           leaves these at 0; ENT_PLATFORM wants enabled=1 by default. */
        if (type == ENT_PLATFORM) {
            e->platform.enabled = 1;
            e->platform.dir = +1;
        }
        if (type == ENT_LIGHT) {           /* white / unit / 4 m unless overridden */
            e->light.r = e->light.g = e->light.b = 1.0f;
            e->light.intensity = 1.0f;
            e->light.radius = 4.0f;
        }

        /* Name and group ("-" means none) */
        if (strcmp(tokens[1], "-") != 0)
            strncpy(e->name, tokens[1], 31);
        if (strcmp(tokens[2], "-") != 0)
            strncpy(e->group, tokens[2], 31);

        /* Transform */
        e->posX = (float)atof(tokens[3]);
        e->posY = (float)atof(tokens[4]);
        e->posZ = (float)atof(tokens[5]);
        e->rotY = (float)atof(tokens[6]);

        /* Parse key=value pairs */
        char meshPath[256] = {0};
        char texPath[256] = {0};
        char iqmPath[256] = {0};
        char initAnim[64] = {0};

        for (int i = 7; i < count; i++) {
            char key[64], value[64];
            if (!entParseKV(tokens[i], key, value)) continue;

            if (strcmp(key, "mesh") == 0) { strncpy(meshPath, assetRegResolveModel(reg, value), 255); strncpy(e->meshName, value, 31); }
            else if (strcmp(key, "tex") == 0) { strncpy(texPath, assetRegResolveTexture(reg, value), 255); strncpy(e->texName, value, 31); }
            else if (strcmp(key, "iqm") == 0) { strncpy(iqmPath, assetRegResolveModel(reg, value), 255); strncpy(e->iqmName, value, 31); }
            else if (strcmp(key, "anim") == 0) { strncpy(initAnim, value, 63); strncpy(e->animName, value, 31); }
            else if (strcmp(key, "scale") == 0) e->scale = (float)atof(value);
            else if (strcmp(key, "static") == 0) e->isStatic = atoi(value);
            else if (strcmp(key, "flip_cull") == 0) e->flipCull = atoi(value);
            else if (strcmp(key, "anim_speed") == 0) e->animSpeed = (float)atof(value);
            /* Type-specific */
            else if (strcmp(key, "item_type") == 0) e->item.itemType = atoi(value);
            else if (strcmp(key, "health") == 0) e->enemy.health = atoi(value);
            else if (strcmp(key, "speed") == 0) {
                float v = (float)atof(value);
                if (type == ENT_DOOR)          e->door.speed = v;
                else if (type == ENT_PLATFORM) e->platform.speed = v;
                else                           e->enemy.speed = v;
            }
            else if (strcmp(key, "sight") == 0) e->enemy.sightRange = (float)atof(value);
            else if (strcmp(key, "path") == 0) strncpy(e->platform.pathGroup, value, 31);
            else if (strcmp(key, "move") == 0) {
                if (strcmp(value, "ping_pong") == 0) e->platform.moveType = PATH_PING_PONG;
                else                                 e->platform.moveType = PATH_ONCE;
            }
            else if (strcmp(key, "enabled") == 0) e->platform.enabled = atoi(value);
            else if (strcmp(key, "face_path") == 0) e->platform.facePath = atoi(value);
            else if (strcmp(key, "target") == 0) {
                if (type == ENT_SWITCH) strncpy(e->sw.target, value, 31);
                else if (type == ENT_TRIGGER) strncpy(e->trigger.target, value, 31);
            }
            else if (strcmp(key, "size") == 0) {
                sscanf(value, "%f,%f,%f", &e->trigger.sizeX, &e->trigger.sizeY, &e->trigger.sizeZ);
            }
            else if (strcmp(key, "once") == 0) e->trigger.once = atoi(value);
            else if (strcmp(key, "order") == 0) e->pathNode.order = atoi(value);
            /* Light keys */
            else if (strcmp(key, "color") == 0)
                sscanf(value, "%f,%f,%f", &e->light.r, &e->light.g, &e->light.b);
            else if (strcmp(key, "intensity") == 0) e->light.intensity = (float)atof(value);
            else if (strcmp(key, "radius") == 0) e->light.radius = (float)atof(value);
            else if (strcmp(key, "collide") == 0) {
                if (strcmp(value, "box") == 0) e->collide = 1;
                else if (strcmp(value, "trimesh") == 0) e->collide = 2;
                else e->collide = atoi(value); /* collide=1 also works */
            }
            /* Door keys */
            else if (strcmp(key, "motion") == 0) {
                if (strcmp(value, "rotate") == 0) e->door.motion = 1;
                else e->door.motion = 0; /* slide */
            }
            else if (strcmp(key, "axis") == 0) {
                if (value[0] == 'X' || value[0] == 'x') e->door.axis = 0;
                else if (value[0] == 'Z' || value[0] == 'z') e->door.axis = 2;
                else e->door.axis = 1; /* Y default */
            }
            else if (strcmp(key, "amount") == 0) e->door.amount = (float)atof(value);
            else if (strcmp(key, "auto_close") == 0) e->door.autoCloseTime = (float)atof(value);
        }

        if (type == ENT_DOOR) {
            /* Cache the closed-state transform so the update loop can derive
               the current transform from progress instead of accumulating
               floating-point drift each tick. */
            e->door.closedX = e->posX;
            e->door.closedY = e->posY;
            e->door.closedZ = e->posZ;
            e->door.closedRotY = e->rotY;
            if (e->door.speed <= 0.0f)  e->door.speed = 1.0f;
            if (e->door.amount <= 0.0f) e->door.amount = (e->door.motion == 1) ? 90.0f : 1.0f;
            /* Default: doors need a collider */
            if (e->collide == 0) e->collide = 1;
        }

        /* Load IQM animated model */
        if (iqmPath[0]) {
            if (iqmLoad(&e->iqmModel, iqmPath)) {
                e->hasAnim = 1;
                iqmLoadTextures(&e->iqmModel, iqmPath, cache);
                if (initAnim[0]) {
                    int ai = iqmFindAnim(&e->iqmModel, initAnim);
                    if (ai >= 0) e->currentAnim = ai;
                }
            }
        }
        /* Load OBJ static mesh */
        else if (meshPath[0]) {
            objInit(&e->mesh);
            if (objLoad(&e->mesh, meshPath)) {
                e->hasMesh = 1;
                if (texPath[0])
                    e->diffuseTex = texCacheGet(cache, texPath, GL_CLAMP_TO_EDGE);
            }
        }

        conLogf("entity: [%d] type=%s name='%s' group='%s' pos=(%.1f,%.1f,%.1f)\n",
               idx, tokens[0], e->name, e->group, e->posX, e->posY, e->posZ);
    }

    fclose(f);
    conLogf("entity: loaded %d entities from %s\n", el->count, filename);
    return 1;
}

/* ---- Update entities ---- */

static void entUpdate(EntityList *el, float playerX, float playerY, float playerZ, float dt)
{
    for (int i = 0; i < el->count; i++) {
        Entity *e = &el->entities[i];
        if (!e->active) continue;

        /* Update animation (default to looping — engine decides, not IQM flag) */
        if (e->hasAnim && e->iqmModel.numAnims > 0) {
            IqmAnim *anim = &e->iqmModel.anims[e->currentAnim];
            e->animTime += dt * e->animSpeed * anim->framerate;
            while (e->animTime >= anim->numFrames)
                e->animTime -= anim->numFrames;
        }

        switch (e->type) {
        case ENT_ITEM:
            if (!e->item.picked) {
                float dx = playerX - e->posX;
                float dy = playerY - e->posY;
                float dz = playerZ - e->posZ;
                float dist = sqrtf(dx*dx + dy*dy + dz*dz);
                if (dist < 1.0f) {
                    e->item.picked = 1;
                    e->active = 0;
                    conLogf("entity: picked up '%s'\n", e->name);
                }
            }
            break;

        case ENT_TRIGGER:
            if (e->trigger.once && e->trigger.triggered) break;
            {
                float dx = playerX - e->posX;
                float dy = playerY - e->posY;
                float dz = playerZ - e->posZ;
                float sx = e->trigger.sizeX > 0 ? e->trigger.sizeX : 1.0f;
                float sy = e->trigger.sizeY > 0 ? e->trigger.sizeY : 1.0f;
                float sz = e->trigger.sizeZ > 0 ? e->trigger.sizeZ : 1.0f;
                if (dx > -sx && dx < sx && dy > -sy && dy < sy && dz > -sz && dz < sz) {
                    if (!e->trigger.triggered) {
                        e->trigger.triggered = 1;
                        conLogf("entity: trigger '%s' fired -> '%s'\n", e->name, e->trigger.target);
                        if (e->trigger.target[0])
                            entActivate(el, e->trigger.target);
                    }
                } else {
                    if (!e->trigger.once) e->trigger.triggered = 0;
                }
            }
            break;

        /* ENT_PLATFORM is advanced by updatePlatforms() in main.cpp, not here.
           It needs the PathTable (held by Game) and the PhysWorld to apply
           collider transforms and rider carrying — both outside entity.h's
           scope. */

        default:
            break;
        }
    }
}

/* ---- Render all visible entities ---- */

static void entRender(EntityList *el)
{
    for (int i = 0; i < el->count; i++) {
        Entity *e = &el->entities[i];
        if (!e->active) continue;
        if (!e->hasMesh && !e->hasAnim) continue;

        glPushMatrix();
        glTranslatef(e->posX, e->posY, e->posZ);
        glRotatef(e->rotY, 0.0f, 1.0f, 0.0f);

        if (e->hasAnim) {
            /* IQM animated model */
            glRotatef(-90.0f, 1.0f, 0.0f, 0.0f); /* Z-up to Y-up */
            glScalef(e->scale, e->scale, e->scale);

            float globalFrame = e->animTime;
            if (e->iqmModel.numAnims > 0) {
                IqmAnim *anim = &e->iqmModel.anims[e->currentAnim];
                globalFrame = anim->firstFrame + e->animTime;
            }
            iqmAnimate(&e->iqmModel, globalFrame);

            if (e->flipCull) glCullFace(GL_FRONT);
            iqmRender(&e->iqmModel);
            if (e->flipCull) glCullFace(GL_BACK);
        }
        else if (e->hasMesh) {
            /* OBJ static mesh */
            glScalef(e->scale, e->scale, e->scale);

            if (e->diffuseTex) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, e->diffuseTex);
            }

            glColor3f(1.0f, 1.0f, 1.0f);
            glBegin(GL_TRIANGLES);
            for (int t = 0; t < e->mesh.numTris; t++) {
                Triangle *tri = &e->mesh.tris[t];
                for (int j = 0; j < 3; j++) {
                    if (tri->n[j] >= 0 && tri->n[j] < e->mesh.numNormals) {
                        Vec3 *n = &e->mesh.normals[tri->n[j]];
                        glNormal3f(n->x, n->y, n->z);
                    }
                    if (tri->t[j] >= 0 && tri->t[j] < e->mesh.numTexcoords) {
                        Vec2 *tc = &e->mesh.texcoords[tri->t[j]];
                        glTexCoord2f(tc->u, tc->v);
                    }
                    Vec3 *v = &e->mesh.verts[tri->v[j]];
                    glVertex3f(v->x, v->y, v->z);
                }
            }
            glEnd();

            if (e->diffuseTex) glDisable(GL_TEXTURE_2D);
        }

        glPopMatrix();
    }
}

/* ---- Cleanup ---- */

static void entListFree(EntityList *el)
{
    for (int i = 0; i < el->count; i++) {
        Entity *e = &el->entities[i];
        if (e->hasMesh) objFree(&e->mesh);
        if (e->hasAnim) iqmFree(&e->iqmModel);
    }
    el->count = 0;
    el->playerIndex = -1;
}

#endif
