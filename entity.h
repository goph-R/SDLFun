#ifndef ENTITY_H
#define ENTITY_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <math.h>

#include "obj_loader.h"
#include "texture.h"
#include "iqm.h"

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
    ENT_TRIGGER
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

    /* Visual: static mesh (OBJ) */
    int hasMesh;
    ObjMesh mesh;
    GLuint diffuseTex;
    int isStatic;       /* baked into lightmap, no physics update */
    int flipCull;       /* 1 = use GL_FRONT culling (flipped winding) */

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
            float startY, endY;
            float speed;
            int state;      /* 0=at start, 1=to end, 2=at end, 3=to start */
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
            case ENT_PLATFORM:
                /* Toggle platform direction */
                if (e->platform.state == 0) e->platform.state = 1;
                else if (e->platform.state == 2) e->platform.state = 3;
                break;
            case ENT_SWITCH:
                e->sw.state = !e->sw.state;
                /* Cascade: activate the switch's own target */
                if (e->sw.target[0])
                    entActivate(el, e->sw.target);
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

static int entLoadFile(EntityList *el, const char *filename, TexCache *cache)
{
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf("entity: no .ent file (%s), using defaults\n", filename);
        return 0;
    }

    /* Extract directory for resolving mesh/texture paths */
    char dir[256];
    dir[0] = '\0';
    const char *lastSlash = strrchr(filename, '/');
    if (!lastSlash) lastSlash = strrchr(filename, '\\');
    if (lastSlash) {
        int dirLen = (int)(lastSlash - filename + 1);
        if (dirLen > 255) dirLen = 255;
        memcpy(dir, filename, dirLen);
        dir[dirLen] = '\0';
    }

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
        else { fprintf(stderr, "entity: unknown type '%s'\n", tokens[0]); continue; }

        int idx = entCreate(el, type);
        if (idx < 0) { fprintf(stderr, "entity: max entities reached\n"); break; }
        Entity *e = &el->entities[idx];

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

            if (strcmp(key, "mesh") == 0) snprintf(meshPath, 256, "%s%s", dir, value);
            else if (strcmp(key, "tex") == 0) snprintf(texPath, 256, "%s%s", dir, value);
            else if (strcmp(key, "iqm") == 0) snprintf(iqmPath, 256, "%s%s", dir, value);
            else if (strcmp(key, "anim") == 0) strncpy(initAnim, value, 63);
            else if (strcmp(key, "scale") == 0) e->scale = (float)atof(value);
            else if (strcmp(key, "static") == 0) e->isStatic = atoi(value);
            else if (strcmp(key, "flip_cull") == 0) e->flipCull = atoi(value);
            else if (strcmp(key, "anim_speed") == 0) e->animSpeed = (float)atof(value);
            /* Type-specific */
            else if (strcmp(key, "item_type") == 0) e->item.itemType = atoi(value);
            else if (strcmp(key, "health") == 0) e->enemy.health = atoi(value);
            else if (strcmp(key, "speed") == 0) e->enemy.speed = (float)atof(value);
            else if (strcmp(key, "sight") == 0) e->enemy.sightRange = (float)atof(value);
            else if (strcmp(key, "start_y") == 0) e->platform.startY = (float)atof(value);
            else if (strcmp(key, "end_y") == 0) e->platform.endY = (float)atof(value);
            else if (strcmp(key, "target") == 0) {
                if (type == ENT_SWITCH) strncpy(e->sw.target, value, 31);
                else if (type == ENT_TRIGGER) strncpy(e->trigger.target, value, 31);
            }
            else if (strcmp(key, "size") == 0) {
                sscanf(value, "%f,%f,%f", &e->trigger.sizeX, &e->trigger.sizeY, &e->trigger.sizeZ);
            }
            else if (strcmp(key, "once") == 0) e->trigger.once = atoi(value);
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

        printf("entity: [%d] type=%s name='%s' group='%s' pos=(%.1f,%.1f,%.1f)\n",
               idx, tokens[0], e->name, e->group, e->posX, e->posY, e->posZ);
    }

    fclose(f);
    printf("entity: loaded %d entities from %s\n", el->count, filename);
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
                    printf("entity: picked up '%s'\n", e->name);
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
                        printf("entity: trigger '%s' fired -> '%s'\n", e->name, e->trigger.target);
                        if (e->trigger.target[0])
                            entActivate(el, e->trigger.target);
                    }
                } else {
                    if (!e->trigger.once) e->trigger.triggered = 0;
                }
            }
            break;

        case ENT_PLATFORM:
            if (e->platform.state == 1) {
                e->posY += e->platform.speed * dt;
                if (e->posY >= e->platform.endY) {
                    e->posY = e->platform.endY;
                    e->platform.state = 2;
                }
            } else if (e->platform.state == 3) {
                e->posY -= e->platform.speed * dt;
                if (e->posY <= e->platform.startY) {
                    e->posY = e->platform.startY;
                    e->platform.state = 0;
                }
            }
            break;

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
