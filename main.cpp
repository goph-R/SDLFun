#ifdef _WIN32
#include <SDL/SDL.h>
#else
#include <SDL.h>
#endif
#include <GL/gl.h>
#include <cstdlib>
#include <cstdio>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "sound.h"
#include "obj_loader.h"
#include "texture.h"
#include "iqm.h"
#include "entity.h"
#include "flashlight.h"
#include "physics.h"
#include "ui.h"

#define SAMPLE_RATE 44100

/* Screen size — defaults overridden by -w / -h command-line args. */
static int SCREEN_W = 640;
static int SCREEN_H = 480;

/* Horizontal FOV held constant; vertical FOV is derived from the current
   aspect ratio so wider screens show more to the sides rather than stretch.
   H = 90° matches the classic Quake/HL1 feel at 4:3. */
#define H_FOV_DEG 90.0f
static float computeVFov(void)
{
    float hRad = H_FOV_DEG * (float)M_PI / 180.0f;
    float aspect = (float)SCREEN_W / (float)SCREEN_H;
    float vRad = 2.0f * atanf(tanf(hRad * 0.5f) / aspect);
    return vRad * 180.0f / (float)M_PI;
}

/* ---- Multitexture (GL_ARB_multitexture) ---- */

#ifndef GL_TEXTURE0_ARB
#define GL_TEXTURE0_ARB 0x84C0
#define GL_TEXTURE1_ARB 0x84C1
#endif

/*
 * On Win98/old MinGW, these GL entry points don't exist in headers,
 * so we load them as function pointers at runtime.
 * On modern Linux, gl.h already declares them as real functions.
 */
#ifdef _WIN32
typedef void (APIENTRY *PFN_MT_ActiveTexture)(GLenum texture);
typedef void (APIENTRY *PFN_MT_MultiTexCoord2f)(GLenum target, GLfloat s, GLfloat t);
static PFN_MT_ActiveTexture p_MT_ActiveTexture = NULL;
static PFN_MT_MultiTexCoord2f p_MT_MultiTexCoord2f = NULL;
#define MT_ActiveTexture p_MT_ActiveTexture
#define MT_MultiTexCoord2f p_MT_MultiTexCoord2f
#else
#define MT_ActiveTexture glActiveTextureARB
#define MT_MultiTexCoord2f glMultiTexCoord2fARB
#endif

static int hasMultitexture = 0;

static void initMultitexture(void)
{
#ifdef _WIN32
    const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
    if (extensions && strstr(extensions, "GL_ARB_multitexture")) {
        p_MT_ActiveTexture = (PFN_MT_ActiveTexture)
            SDL_GL_GetProcAddress("glActiveTextureARB");
        p_MT_MultiTexCoord2f = (PFN_MT_MultiTexCoord2f)
            SDL_GL_GetProcAddress("glMultiTexCoord2fARB");
        if (p_MT_ActiveTexture && p_MT_MultiTexCoord2f) {
            hasMultitexture = 1;
            printf("Multitexture: supported\n");
        }
    }
#else
    /* Modern Linux GL always has multitexture */
    hasMultitexture = 1;
    printf("Multitexture: supported\n");
#endif
    if (!hasMultitexture) {
        printf("Multitexture: not available (lightmaps disabled)\n");
    }
}

/* ---- Sound helpers ----
 * Each generator fills a plain short[] with 16-bit signed mono PCM, then
 * hands it to sndMakeBuffer so the backend (FMOD or OpenAL) can upload it.
 */

static SoundBuffer createTone(float freq, int length)
{
    short *buf = (short *)malloc(length * sizeof(short));
    for (int i = 0; i < length; i++) {
        float t = (float)i / SAMPLE_RATE;
        float envelope = 1.0f - (float)i / length;
        buf[i] = (short)(sinf(2.0f * (float)M_PI * freq * t) * 32000.0f * envelope);
    }
    SoundBuffer s = sndMakeBuffer(buf, length, SAMPLE_RATE);
    free(buf);
    return s;
}

static SoundBuffer createGunshot(void)
{
    int length = 4410;
    short *buf = (short *)malloc(length * sizeof(short));
    for (int i = 0; i < length; i++) {
        float envelope = 1.0f - (float)i / length;
        envelope *= envelope;
        float noise = (float)(rand() % 65536 - 32768) / 32768.0f;
        buf[i] = (short)(noise * 28000.0f * envelope);
    }
    SoundBuffer s = sndMakeBuffer(buf, length, SAMPLE_RATE);
    free(buf);
    return s;
}

static SoundBuffer createFootstep(void)
{
    int length = 2205;
    short *buf = (short *)malloc(length * sizeof(short));
    for (int i = 0; i < length; i++) {
        float envelope = 1.0f - (float)i / length;
        float thud  = sinf(2.0f * (float)M_PI * 80.0f * (float)i / SAMPLE_RATE);
        float noise = (float)(rand() % 65536 - 32768) / 32768.0f;
        buf[i] = (short)((thud * 0.7f + noise * 0.3f) * 20000.0f * envelope);
    }
    SoundBuffer s = sndMakeBuffer(buf, length, SAMPLE_RATE);
    free(buf);
    return s;
}

/* ---- OpenGL helpers ---- */

static void glSetPerspective(float fovDeg, float aspect, float zNear, float zFar)
{
    float fovRad = fovDeg * (float)M_PI / 180.0f;
    float f = 1.0f / tanf(fovRad * 0.5f);
    float m[16];
    memset(m, 0, sizeof(m));
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zFar + zNear) / (zNear - zFar);
    m[11] = -1.0f;
    m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    glMultMatrixf(m);
}

static void glLookAt(float eyeX, float eyeY, float eyeZ,
                     float atX, float atY, float atZ)
{
    float fx = atX - eyeX;
    float fy = atY - eyeY;
    float fz = atZ - eyeZ;
    float flen = sqrtf(fx*fx + fy*fy + fz*fz);
    fx /= flen; fy /= flen; fz /= flen;

    /* right = normalize(cross(forward, (0,1,0))) */
    float rx = -fz;
    float ry = 0.0f;
    float rz = fx;
    float rlen = sqrtf(rx*rx + rz*rz);
    if (rlen > 0.0001f) { rx /= rlen; rz /= rlen; }

    /* true up = cross(right, forward) */
    float ux = ry*fz - rz*fy;
    float uy = rz*fx - rx*fz;
    float uz = rx*fy - ry*fx;

    float mat[16] = {
         rx,  ux, -fx, 0,
         ry,  uy, -fy, 0,
         rz,  uz, -fz, 0,
          0,   0,   0, 1
    };
    glMultMatrixf(mat);
    glTranslatef(-eyeX, -eyeY, -eyeZ);
}

/* ---- Tiling UV computation (Quake-style box mapping) ---- */

static void computeTilingUV(Vec3 *pos, Vec3 *normal, float scale,
                            float offU, float offV, float *ou, float *ov)
{
    float ax = normal->x; if (ax < 0) ax = -ax;
    float ay = normal->y; if (ay < 0) ay = -ay;
    float az = normal->z; if (az < 0) az = -az;

    if (ay >= ax && ay >= az) {
        /* floor/ceiling: project onto XZ */
        *ou = pos->x * scale + offU;
        *ov = pos->z * scale + offV;
    } else if (ax >= az) {
        /* left/right wall: project onto ZY */
        *ou = pos->z * scale + offU;
        *ov = pos->y * scale + offV;
    } else {
        /* front/back wall: project onto XY */
        *ou = pos->x * scale + offU;
        *ov = pos->y * scale + offV;
    }
}

/* ---- Level rendering ---- */

static void setColorByNormal(Vec3 *n)
{
    if (n->y > 0.5f) {
        glColor3f(0.5f, 0.5f, 0.5f);
    } else if (n->y < -0.5f) {
        glColor3f(0.3f, 0.3f, 0.35f);
    } else if (n->y > 0.1f) {
        glColor3f(0.6f, 0.5f, 0.35f);
    } else {
        glColor3f(0.6f, 0.35f, 0.3f);
    }
}

static void renderLevel(ObjMesh *mesh, GLuint diffuseTex, GLuint lightmapTex)
{
    int hasDiffuse = (diffuseTex != 0 && mesh->numTexcoords > 0);
    int hasLightmap = (lightmapTex != 0 && hasMultitexture && mesh->numTexcoords > 0);

    /* When lightmaps are active, disable GL_LIGHTING — the lightmap already
       contains all lighting information. GL_LIGHTING would add directional
       bias (e.g., darkening ceilings whose normals face away from GL_LIGHT0). */
    if (hasLightmap) glDisable(GL_LIGHTING);

    if (hasDiffuse) {
        if (hasLightmap) {
            /* Unit 0: diffuse texture */
            MT_ActiveTexture(GL_TEXTURE0_ARB);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, diffuseTex);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

            /* Unit 1: lightmap (modulate on top) */
            MT_ActiveTexture(GL_TEXTURE1_ARB);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, lightmapTex);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        } else {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, diffuseTex);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        }
    }

    glBegin(GL_TRIANGLES);
    for (int i = 0; i < mesh->numTris; i++) {
        Triangle *t = &mesh->tris[i];
        Vec3 *n = (mesh->numNormals > 0 && t->n[0] >= 0 && t->n[0] < mesh->numNormals)
                   ? &mesh->normals[t->n[0]] : NULL;

        if (!hasDiffuse && n) {
            setColorByNormal(n);
        } else if (!hasDiffuse) {
            glColor3f(0.5f, 0.5f, 0.5f);
        } else {
            glColor3f(1.0f, 1.0f, 1.0f);
        }

        for (int j = 0; j < 3; j++) {
            if (n && t->n[j] >= 0 && t->n[j] < mesh->numNormals) {
                Vec3 *nn = &mesh->normals[t->n[j]];
                glNormal3f(nn->x, nn->y, nn->z);
            }
            if (hasDiffuse && t->t[j] >= 0 && t->t[j] < mesh->numTexcoords) {
                Vec2 *tc = &mesh->texcoords[t->t[j]];
                if (hasLightmap) {
                    /* Same UVs for both diffuse and lightmap
                       (lightmap uses its own UV in practice,
                        but for single-UV-set OBJ this works) */
                    MT_MultiTexCoord2f(GL_TEXTURE0_ARB, tc->u, tc->v);
                    MT_MultiTexCoord2f(GL_TEXTURE1_ARB, tc->u, tc->v);
                } else {
                    glTexCoord2f(tc->u, tc->v);
                }
            }
            Vec3 *v = &mesh->verts[t->v[j]];
            glVertex3f(v->x, v->y, v->z);
        }
    }
    glEnd();

    /* Clean up texture state */
    if (hasLightmap) {
        MT_ActiveTexture(GL_TEXTURE1_ARB);
        glDisable(GL_TEXTURE_2D);
        MT_ActiveTexture(GL_TEXTURE0_ARB);
        glEnable(GL_LIGHTING); /* restore for entities/gun */
    }
    if (hasDiffuse) {
        glDisable(GL_TEXTURE_2D);
    }
}

/* ---- Sectored level rendering (multi-material + per-sector lightmaps) ---- */

static void renderLevelSectored(ObjMesh *mesh, TexCache *cache)
{
    /* Backward compat: no sectors -> use legacy single-texture path */
    if (mesh->numSectors == 0) {
        GLuint diffTex = texCacheGet(cache, "diffuse.bmp", GL_CLAMP_TO_EDGE);
        GLuint lmTex  = texCacheGet(cache, "lightmap.bmp", GL_CLAMP_TO_EDGE);
        renderLevel(mesh, diffTex, lmTex);
        return;
    }

    for (int s = 0; s < mesh->numSectors; s++) {
        Sector *sec = &mesh->sectors[s];
        Material *mat = NULL;
        GLuint diffTex = 0;
        GLuint lmTex = 0;
        float tileScale = 1.0f;

        if (sec->materialId >= 0 && sec->materialId < mesh->numMaterials) {
            mat = &mesh->materials[sec->materialId];
            diffTex = texCacheGet(cache, mat->diffusePath, GL_REPEAT);
            lmTex   = texCacheGet(cache, mat->lightmapPath, GL_CLAMP_TO_EDGE);
            tileScale = mat->tilingScale;
        }

        float tileOffU = mat ? mat->tilingOffsetX : 0.0f;
        float tileOffV = mat ? mat->tilingOffsetY : 0.0f;
        int hasDiffuse = (diffTex != 0);
        int hasLM = (lmTex != 0 && hasMultitexture && mesh->numTexcoords > 0);

        /* Set up texture units */
        if (hasDiffuse) {
            if (hasLM) {
                MT_ActiveTexture(GL_TEXTURE0_ARB);
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, diffTex);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

                MT_ActiveTexture(GL_TEXTURE1_ARB);
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, lmTex);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            } else {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, diffTex);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            }
        }

        glBegin(GL_TRIANGLES);
        for (int i = sec->triStart; i < sec->triStart + sec->triCount; i++) {
            Triangle *t = &mesh->tris[i];
            Vec3 *n = (mesh->numNormals > 0 && t->n[0] >= 0 && t->n[0] < mesh->numNormals)
                       ? &mesh->normals[t->n[0]] : NULL;

            if (!hasDiffuse && n) {
                setColorByNormal(n);
            } else if (!hasDiffuse) {
                glColor3f(0.5f, 0.5f, 0.5f);
            } else {
                glColor3f(1.0f, 1.0f, 1.0f);
            }

            for (int j = 0; j < 3; j++) {
                Vec3 *nn = NULL;
                if (n && t->n[j] >= 0 && t->n[j] < mesh->numNormals) {
                    nn = &mesh->normals[t->n[j]];
                    glNormal3f(nn->x, nn->y, nn->z);
                }

                Vec3 *v = &mesh->verts[t->v[j]];

                if (hasDiffuse) {
                    /* Diffuse: tiling UV from world position */
                    Vec3 faceN = n ? *n : (Vec3){0, 1, 0};
                    float du, dv;
                    computeTilingUV(v, &faceN, tileScale, tileOffU, tileOffV, &du, &dv);

                    if (hasLM && t->t[j] >= 0 && t->t[j] < mesh->numTexcoords) {
                        Vec2 *tc = &mesh->texcoords[t->t[j]];
                        MT_MultiTexCoord2f(GL_TEXTURE0_ARB, du, dv);
                        MT_MultiTexCoord2f(GL_TEXTURE1_ARB, tc->u, tc->v);
                    } else {
                        glTexCoord2f(du, dv);
                    }
                }

                glVertex3f(v->x, v->y, v->z);
            }
        }
        glEnd();

        /* Clean up texture state for this sector */
        if (hasLM) {
            MT_ActiveTexture(GL_TEXTURE1_ARB);
            glDisable(GL_TEXTURE_2D);
            MT_ActiveTexture(GL_TEXTURE0_ARB);
        }
        if (hasDiffuse) {
            glDisable(GL_TEXTURE_2D);
        }
    }
}

/* ---- Crosshair ---- */

static void renderCrosshair(void)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, SCREEN_W, SCREEN_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);

    float cx = SCREEN_W / 2.0f;
    float cy = SCREEN_H / 2.0f;
    float size = 10.0f;

    glLineWidth(2.0f);
    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_LINES);
        glVertex2f(cx - size, cy);
        glVertex2f(cx + size, cy);
        glVertex2f(cx, cy - size);
        glVertex2f(cx, cy + size);
    glEnd();

    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

/* ---- Simple gun rendering (first person) ---- */

static void renderGun(int flashTimer)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glSetPerspective(60.0f, (float)SCREEN_W / SCREEN_H, 0.01f, 10.0f);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_LIGHTING);
    glClear(GL_DEPTH_BUFFER_BIT);

    glTranslatef(0.3f, -0.3f, -0.6f);
    glRotatef(-5.0f, 0, 0, 1);

    glColor3f(0.2f, 0.2f, 0.25f);
    glBegin(GL_QUADS);
        /* Top */
        glVertex3f(-0.05f, 0.05f, -0.2f);
        glVertex3f( 0.05f, 0.05f, -0.2f);
        glVertex3f( 0.05f, 0.05f,  0.1f);
        glVertex3f(-0.05f, 0.05f,  0.1f);
        /* Bottom */
        glVertex3f(-0.05f, -0.05f, -0.2f);
        glVertex3f(-0.05f, -0.05f,  0.1f);
        glVertex3f( 0.05f, -0.05f,  0.1f);
        glVertex3f( 0.05f, -0.05f, -0.2f);
        /* Left */
        glVertex3f(-0.05f, -0.05f, -0.2f);
        glVertex3f(-0.05f,  0.05f, -0.2f);
        glVertex3f(-0.05f,  0.05f,  0.1f);
        glVertex3f(-0.05f, -0.05f,  0.1f);
        /* Right */
        glVertex3f(0.05f, -0.05f, -0.2f);
        glVertex3f(0.05f, -0.05f,  0.1f);
        glVertex3f(0.05f,  0.05f,  0.1f);
        glVertex3f(0.05f,  0.05f, -0.2f);
        /* Front (barrel end) */
        glVertex3f(-0.05f, -0.05f, -0.2f);
        glVertex3f( 0.05f, -0.05f, -0.2f);
        glVertex3f( 0.05f,  0.05f, -0.2f);
        glVertex3f(-0.05f,  0.05f, -0.2f);
        /* Back */
        glVertex3f(-0.05f, -0.05f,  0.1f);
        glVertex3f(-0.05f,  0.05f,  0.1f);
        glVertex3f( 0.05f,  0.05f,  0.1f);
        glVertex3f( 0.05f, -0.05f,  0.1f);
    glEnd();

    /* Muzzle flash */
    if (flashTimer > 0) {
        glColor3f(1.0f, 0.8f, 0.2f);
        glBegin(GL_TRIANGLES);
            glVertex3f( 0.0f,  0.0f, -0.22f);
            glVertex3f(-0.08f, 0.08f, -0.35f);
            glVertex3f( 0.08f, 0.08f, -0.35f);
            glVertex3f( 0.0f,  0.0f, -0.22f);
            glVertex3f(-0.06f,-0.02f, -0.32f);
            glVertex3f( 0.06f,-0.02f, -0.32f);
        glEnd();
    }

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

/* ---- Door update ----
 * Advances each opening/closing door by speed*dt along its path. Asks the
 * physics layer to sweep its kinematic body from current to target; if the
 * sweep hits anything (player, decoration, other door), the door pauses
 * this tick and retries next tick. When progress reaches 0 or 1, the door
 * settles into the closed/open state.
 */
static void updateDoors(EntityList *el, PhysWorld *pw, float dt)
{
    for (int i = 0; i < el->count; i++) {
        Entity *e = &el->entities[i];
        if (!e->active || e->type != ENT_DOOR || !e->physBody) continue;

        /* Auto-close: if door is fully open and auto_close was set, count up.
           When the timer exceeds the configured duration, transition to
           closing. Blocked closes don't reset the timer — it's advisory. */
        if (e->door.state == 2 && e->door.autoCloseTime > 0.0f) {
            e->door.openTimer += dt;
            if (e->door.openTimer >= e->door.autoCloseTime) {
                e->door.state = 3;
                e->door.openTimer = 0.0f;
            }
        }

        if (e->door.state != 1 && e->door.state != 3) continue;

        float delta = (e->door.speed * dt) / e->door.amount;
        float target = e->door.progress + (e->door.state == 1 ? delta : -delta);
        if (target > 1.0f) target = 1.0f;
        if (target < 0.0f) target = 0.0f;

        /* Target entity transform derived from the closed pose + progress. */
        float nx = e->door.closedX, ny = e->door.closedY, nz = e->door.closedZ;
        float nr = e->door.closedRotY;
        if (e->door.motion == 0) {
            float off = target * e->door.amount;
            if      (e->door.axis == 0) nx += off;
            else if (e->door.axis == 1) ny += off;
            else                        nz += off;
        } else {
            nr += target * e->door.amount;
        }

        /* Collider world center = entity position + rotated local AABB offset. */
        float rad = nr * (float)M_PI / 180.0f;
        float cs = cosf(rad), sn = sinf(rad);
        float cx = nx + (cs * e->door.lcx + sn * e->door.lcz);
        float cy = ny + e->door.lcy;
        float cz = nz + (-sn * e->door.lcx + cs * e->door.lcz);

        if (physMoveKinematicBox(pw, e->physBody, cx, cy, cz, nr)) {
            e->door.progress = target;
            e->posX = nx; e->posY = ny; e->posZ = nz; e->rotY = nr;
            if (target >= 1.0f) {
                e->door.state = 2;
                e->door.openTimer = 0.0f;
            }
            else if (target <= 0.0f) e->door.state = 0;
        }
        /* else: blocked, keep progress/state unchanged, try again next tick */
    }
}

/* ---- Main ---- */

int main(int argc, char *argv[])
{
    /* Parse -w <width>, -h <height>, -fullscreen. Anything else ignored. */
    int wSpecified = 0, hSpecified = 0, fullscreen = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            SCREEN_W = atoi(argv[++i]);
            wSpecified = 1;
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            SCREEN_H = atoi(argv[++i]);
            hSpecified = 1;
        } else if (strcmp(argv[i], "-fullscreen") == 0) {
            fullscreen = 1;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* If fullscreen with no explicit size, fill any unspecified dimension
       with the desktop resolution. SDL_GetVideoInfo()->current_w/h returns
       the desktop size BEFORE SDL_SetVideoMode has been called (SDL 1.2.10+). */
    if (fullscreen) {
        const SDL_VideoInfo *vi = SDL_GetVideoInfo();
        if (vi) {
            if (!wSpecified) SCREEN_W = vi->current_w;
            if (!hSpecified) SCREEN_H = vi->current_h;
        }
    }
    if (SCREEN_W < 320) SCREEN_W = 320;
    if (SCREEN_H < 240) SCREEN_H = 240;
    printf("Resolution: %dx%d%s (V-FOV %.1f deg for %d deg H-FOV)\n",
           SCREEN_W, SCREEN_H, fullscreen ? " fullscreen" : "",
           computeVFov(), (int)H_FOV_DEG);

    SoundSystem snd;
    if (!sndInit(&snd, SAMPLE_RATE)) {
        SDL_Quit();
        return 1;
    }

    SoundBuffer sndGunshot  = createGunshot();
    SoundBuffer sndFootstep = createFootstep();
    SoundBuffer sndJump     = createTone(220.0f, 4410);

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    Uint32 videoFlags = SDL_OPENGL;
    if (fullscreen) videoFlags |= SDL_FULLSCREEN;
    SDL_Surface *screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 32, videoFlags);
    if (!screen) {
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        sndShutdown(&snd);
        SDL_Quit();
        return 1;
    }

    SDL_WM_SetCaption("FPS Demo - SDL + OpenGL + Bullet + OpenAL", NULL);
    SDL_WM_GrabInput(SDL_GRAB_ON);
    SDL_ShowCursor(SDL_DISABLE);

    /* OpenGL setup */
    glViewport(0, 0, SCREEN_W, SCREEN_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glSetPerspective(computeVFov(), (float)SCREEN_W / SCREEN_H, 0.1f, 200.0f);
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    /* Lighting (used as fallback when no lightmap) */
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    float lightPos[] = { 0.0f, 10.0f, 0.0f, 1.0f };
    float lightAmb[] = { 0.3f, 0.3f, 0.3f, 1.0f };
    float lightDif[] = { 0.7f, 0.7f, 0.7f, 1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDif);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    /* Init multitexture extension */
    initMultitexture();

    /* Init UI / HUD (builds bitmap font atlas as a GL texture) */
    UiState ui;
    uiInit(&ui, SCREEN_W, SCREEN_H);

    /* Texture cache (loads textures on demand) */
    TexCache texCache;
    texCacheInit(&texCache);

    /* Load level */
    ObjMesh level;
    objInit(&level);
    if (!objLoad(&level, "test_level.obj")) {
        fprintf(stderr, "Failed to load test_level.obj\n");
        sndShutdown(&snd);
        SDL_Quit();
        return 1;
    }
    printf("Level loaded: %d verts, %d texcoords, %d tris, %d materials\n",
           level.numVerts, level.numTexcoords, level.numTris, level.numMaterials);

    /* Load entities (before physics, to get player spawn position)
       Heap-allocated: EntityList is ~4MB (256 entities with inline ObjMesh+IqmModel) */
    EntityList *entities = (EntityList *)malloc(sizeof(EntityList));
    entListInit(entities);
    entLoadFile(entities, "test_level.ent", &texCache);

    float spawnX = 0.0f, spawnY = 2.0f, spawnZ = 0.0f;
    if (entities->playerIndex >= 0) {
        Entity *pe = &entities->entities[entities->playerIndex];
        spawnX = pe->posX;
        spawnY = pe->posY;
        spawnZ = pe->posZ;
    }

    /* Init physics BEFORE building sectors (sorting changes triangle order) */
    PhysWorld phys;
    physInit(&phys);
    physLoadLevel(&phys, &level);
    physCreatePlayer(&phys, spawnX, spawnY, spawnZ);

    /* Build sector batches (sorts triangles by material) */
    objBuildSectors(&level);

    /* Create colliders for decorations that opted in with
       collide=box (AABB from mesh verts) or collide=trimesh (exact
       btBvhTriangleMeshShape). AABB is fast but wrong for concave
       props (desks with kneeholes, chairs with arm gaps) because
       it fills in the hollow space. */
    for (int i = 0; i < entities->count; i++) {
        Entity *e = &entities->entities[i];
        if (!e->collide || !e->hasMesh || e->mesh.numVerts == 0) continue;

        float s = (e->scale > 0.0f) ? e->scale : 1.0f;

        if (e->collide == 2) {
            /* Trimesh: position is the entity origin; scale and rotation
               are applied by the shape/body respectively. */
            e->physBody = physAddStaticTrimesh(&phys, &e->mesh,
                                               e->posX, e->posY, e->posZ,
                                               e->rotY, s);
            printf("entity: %s collider (trimesh %d tris at %.2f,%.2f,%.2f)\n",
                   e->name, e->mesh.numTris, e->posX, e->posY, e->posZ);
            continue;
        }

        /* Box: AABB of mesh verts, scaled, then rotated around entity origin. */
        Vec3 *v0 = &e->mesh.verts[0];
        float minX = v0->x, maxX = v0->x;
        float minY = v0->y, maxY = v0->y;
        float minZ = v0->z, maxZ = v0->z;
        for (int k = 1; k < e->mesh.numVerts; k++) {
            Vec3 *v = &e->mesh.verts[k];
            if (v->x < minX) minX = v->x; else if (v->x > maxX) maxX = v->x;
            if (v->y < minY) minY = v->y; else if (v->y > maxY) maxY = v->y;
            if (v->z < minZ) minZ = v->z; else if (v->z > maxZ) maxZ = v->z;
        }

        float hx = (maxX - minX) * 0.5f * s;
        float hy = (maxY - minY) * 0.5f * s;
        float hz = (maxZ - minZ) * 0.5f * s;
        float lcx = (minX + maxX) * 0.5f * s;
        float lcy = (minY + maxY) * 0.5f * s;
        float lcz = (minZ + maxZ) * 0.5f * s;

        float rad = e->rotY * (float)M_PI / 180.0f;
        float cs = cosf(rad), sn = sinf(rad);
        float wx = e->posX + (cs * lcx + sn * lcz);
        float wy = e->posY + lcy;
        float wz = e->posZ + (-sn * lcx + cs * lcz);

        if (e->type == ENT_DOOR) {
            /* Door uses a kinematic rigid body so updateDoors() can reposition
               it each frame and Bullet pushes the character controller out of
               the way. Cache the local AABB center so we can recompute the
               world collider transform cheaply when the entity moves. */
            e->door.lcx = lcx;
            e->door.lcy = lcy;
            e->door.lcz = lcz;
            e->physBody = physAddKinematicBox(&phys, wx, wy, wz, hx, hy, hz, e->rotY);
            printf("entity: %s door (kinematic box %.2fx%.2fx%.2f)\n",
                   e->name, hx*2, hy*2, hz*2);
        } else {
            e->physBody = physAddStaticBox(&phys, wx, wy, wz, hx, hy, hz, e->rotY);
            printf("entity: %s collider (box %.2fx%.2fx%.2f at %.2f,%.2f,%.2f)\n",
                   e->name, hx*2, hy*2, hz*2, wx, wy, wz);
        }
    }

    /* Init dynamic lightmap flashlight (HL1-style: modifies lightmap pixels on CPU) */
    DynLightmap dynLm;
    int hasDynLm = 0;
    {
        GLuint lmTex = texCacheGet(&texCache, "lightmap.bmp", GL_CLAMP_TO_EDGE);
        if (lmTex) {
            hasDynLm = dynLmInit(&dynLm, "lightmap.bmp", &level, lmTex);
        }
    }

    /* Camera state */
    float yaw = 0.0f;
    float pitch = 0.0f;
    float moveSpeed = 6.0f;

    int gunFlashTimer = 0;
    int footstepTimer = 0;
    int flashlightOn = 0;
    int debugColliders = 0;

    Uint32 lastTime = SDL_GetTicks();

    SDL_Event event;
    int running = 1;

    while (running) {
        Uint32 now = SDL_GetTicks();
        float dt = (now - lastTime) / 1000.0f;
        if (dt > 0.15f) dt = 0.15f;
        lastTime = now;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = 0;
                }
                if (event.key.keysym.sym == SDLK_f) {
                    flashlightOn = !flashlightOn;
                    printf("Flashlight: %s\n", flashlightOn ? "ON" : "OFF");
                }
                if (event.key.keysym.sym == SDLK_b) {
                    debugColliders = !debugColliders;
                    printf("Collider wireframes: %s\n", debugColliders ? "ON" : "OFF");
                }
                if (event.key.keysym.sym == SDLK_p) {
                    float _px, _py, _pz;
                    physGetPlayerPos(&phys, &_px, &_py, &_pz);
                    printf("player pos: X=%.3f Y=%.3f Z=%.3f  yaw=%.1f pitch=%.1f\n",
                           _px, _py, _pz, yaw, pitch);
                }
                if (event.key.keysym.sym == SDLK_SPACE) {
                    if (phys.character->onGround()) {
                        phys.character->jump();
                        sndPlay(&snd, sndJump);
                    }
                }
            }
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    gunFlashTimer = 4;
                    sndPlay(&snd, sndGunshot);
                }
            }
            if (event.type == SDL_MOUSEMOTION) {
                yaw   -= event.motion.xrel * 0.15f;
                pitch -= event.motion.yrel * 0.15f;
                if (pitch > 89.0f) pitch = 89.0f;
                if (pitch < -89.0f) pitch = -89.0f;
            }
        }

        /* WASD movement */
        Uint8 *keys = SDL_GetKeyState(NULL);
        float forwardX = -sinf(yaw * (float)M_PI / 180.0f);
        float forwardZ = -cosf(yaw * (float)M_PI / 180.0f);
        float rightX = cosf(yaw * (float)M_PI / 180.0f);
        float rightZ = -sinf(yaw * (float)M_PI / 180.0f);

        /* Desired (wish) velocity in m/s from input. */
        float wishX = 0, wishZ = 0;
        if (keys[SDLK_w]) { wishX += forwardX; wishZ += forwardZ; }
        if (keys[SDLK_s]) { wishX -= forwardX; wishZ -= forwardZ; }
        if (keys[SDLK_a]) { wishX -= rightX;   wishZ -= rightZ; }
        if (keys[SDLK_d]) { wishX += rightX;   wishZ += rightZ; }
        float wishLen = sqrtf(wishX * wishX + wishZ * wishZ);
        if (wishLen > 0.001f) {
            wishX = (wishX / wishLen) * moveSpeed;
            wishZ = (wishZ / wishLen) * moveSpeed;
        }

        /* HL1-style acceleration + sliding. Accel brings velocity toward the
           wish velocity; friction applies when no input. Higher accel = more
           responsive; higher friction = shorter slide. Numbers roughly match
           GoldSrc defaults scaled to our 6 m/s max. */
        static float velX = 0, velZ = 0;
        const float ACCEL    = 10.0f;   /* m/s²-ish pull toward wish velocity */
        const float FRICTION = 20.0f;   /* m/s²-ish slowdown when no input    */
        if (wishLen > 0.001f) {
            velX += (wishX - velX) * ACCEL * dt;
            velZ += (wishZ - velZ) * ACCEL * dt;
        } else {
            float speed = sqrtf(velX * velX + velZ * velZ);
            if (speed > 0.001f) {
                float drop = FRICTION * dt;
                float newSpeed = speed - drop;
                if (newSpeed < 0) newSpeed = 0;
                velX *= newSpeed / speed;
                velZ *= newSpeed / speed;
            } else {
                velX = 0; velZ = 0;
            }
        }
        int isMoving = (velX * velX + velZ * velZ) > 0.04f;  /* > 0.2 m/s */

        /* Convert velocity (m/s) to per-substep displacement for Bullet. */
        const float perStep = 1.0f / 120.0f;
        phys.character->setWalkDirection(
            btVector3(velX * perStep, 0, velZ * perStep));

        if (isMoving && phys.character->onGround()) {
            footstepTimer -= (int)(dt * 1000);
            if (footstepTimer <= 0) {
                sndPlay(&snd, sndFootstep);
                footstepTimer = 400;
            }
        } else {
            footstepTimer = 0;
        }

        physStep(&phys, dt);

        float px, py, pz;
        physGetPlayerPos(&phys, &px, &py, &pz);
        /* Capsule center is 0.875m above the floor when grounded (halfHeight
           0.525 + radius 0.35), so +0.755 puts the camera at 1.63m — realistic
           eye height for a 1.75m-tall person. */
        float eyeY = py + 0.765f;

        float lookX = px + forwardX;
        float lookY = eyeY + sinf(pitch * (float)M_PI / 180.0f);
        float lookZ = pz + forwardZ;

        if (gunFlashTimer > 0) gunFlashTimer--;

        /* ---- Render ---- */
        glClearColor(0.4f, 0.6f, 0.8f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_LIGHTING);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        /* Camera roll on strafe (HL1 v_iroll_angle): the velocity component
           along the right vector produces a bank angle, clamped to MAX_ROLL.
           Positive strafe (moving right) tilts head right, which in view
           space is a CCW rotation around the view's Z axis. The rotation
           goes BEFORE glLookAt so matrix order is M = R * L (roll applied
           in view space, lookat transforms world→view first). */
        {
            float strafeVel = velX * rightX + velZ * rightZ;
            const float ROLL_SPEED = 6.0f;    /* vel at which we hit max roll */
            const float MAX_ROLL   = 2.0f;    /* degrees */
            float roll = strafeVel / ROLL_SPEED * MAX_ROLL;
            if (roll >  MAX_ROLL) roll =  MAX_ROLL;
            if (roll < -MAX_ROLL) roll = -MAX_ROLL;
            glRotatef(roll, 0.0f, 0.0f, 1.0f);
        }

        glLookAt(px, eyeY, pz, lookX, lookY, lookZ);

        /* Light position must be submitted AFTER the view matrix is on the
           stack — glLightfv transforms the position by the current modelview,
           so this anchors the light at world (0, 10, 0). */
        float lp[] = { 0.0f, 10.0f, 0.0f, 1.0f };
        glLightfv(GL_LIGHT0, GL_POSITION, lp);

        /* Flashlight (HL1-style: modify lightmap pixels on CPU, re-upload) */
        if (flashlightOn && hasDynLm) {
            float dirX = lookX - px;
            float dirY = lookY - eyeY;
            float dirZ = lookZ - pz;
            float dirLen = sqrtf(dirX*dirX + dirY*dirY + dirZ*dirZ);
            if (dirLen > 0.0001f) { dirX /= dirLen; dirY /= dirLen; dirZ /= dirLen; }

            float hitX, hitY, hitZ;
            if (physRaycast(&phys, px, eyeY, pz, dirX, dirY, dirZ, 30.0f,
                            &hitX, &hitY, &hitZ)) {
                dynLmUpdate(&dynLm, hitX, hitY, hitZ,
                            3.0f,           /* radius in meters */
                            1.0f,           /* intensity */
                            1.0f, 0.95f, 0.8f); /* warm white color */
            } else {
                dynLmRestore(&dynLm);
            }
        } else if (hasDynLm) {
            dynLmRestore(&dynLm);
        }

        renderLevelSectored(&level, &texCache);

        /* Update and render entities */
        entUpdate(entities, px, py, pz, dt);
        updateDoors(entities, &phys, dt);
        glEnable(GL_LIGHTING);
        glColor3f(1.0f, 1.0f, 1.0f);
        entRender(entities);

        if (debugColliders) physDebugDrawColliders(&phys);

        renderGun(gunFlashTimer);
        renderCrosshair();

        /* HUD — placeholder values until player/weapon state exists. */
        uiBegin(&ui);
        {
            const float pad = 16.0f;
            const float barW = 180.0f, barH = 18.0f;
            UiColor white = uiRgb(1, 1, 1);
            UiColor red   = uiRgb(0.85f, 0.2f, 0.2f);
            UiColor amber = uiRgb(1.0f, 1.0f, 0.2f);

            /* Health: bottom-left */
            uiQuad(uiRectMake(pad - 4, SCREEN_H - pad - barH - 20, 80, 14),
                   uiRgba(0, 0, 0, 0.55f));
            uiText(&ui, pad, SCREEN_H - pad - barH - 18, 1.25f,
                   white, "HEALTH");
            uiBar(uiRectMake(pad, SCREEN_H - pad - barH, barW, barH),
                  0.87f, red);
            uiText(&ui, pad + barW + 12, SCREEN_H - pad - barH + 2, 1.5f,
                   white, "87");

            /* Ammo: bottom-right */
            char ammo[32];
            snprintf(ammo, sizeof(ammo), "%d / %d", 24, 72);
            float ammoW = (float)strlen(ammo) * 8.0f * 2.0f;  /* scale 2 */
            uiText(&ui, SCREEN_W - pad - ammoW, SCREEN_H - pad - 16, 2.0f,
                   amber, ammo);
        }
        uiEnd(&ui);

        SDL_GL_SwapBuffers();
        SDL_Delay(1);
    }

    /* Cleanup */
    uiShutdown(&ui);
    if (hasDynLm) dynLmFree(&dynLm);
    for (int i = 0; i < entities->count; i++) {
        Entity *e = &entities->entities[i];
        if (!e->physBody) continue;
        if (e->collide == 2) physRemoveStaticTrimesh(&phys, e->physBody);
        else physRemoveStaticBox(&phys, e->physBody);
        e->physBody = NULL;
    }
    entListFree(entities);
    free(entities);
    texCacheFree(&texCache);
    physCleanup(&phys);
    objFree(&level);

    sndFreeBuffer(sndGunshot);
    sndFreeBuffer(sndFootstep);
    sndFreeBuffer(sndJump);
    sndShutdown(&snd);
    SDL_Quit();
    return 0;
}
