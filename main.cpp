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
#include "asset_registry.h"
#include "entity.h"
#include "flashlight.h"
#include "physics.h"
#include "nav.h"
#include "ui.h"
#include "script.h"
#include "game.h"
#include "menu.h"

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

/* ---- Windows refresh-rate preservation ----
 *
 * SDL 1.2's fullscreen path calls ChangeDisplaySettings without specifying
 * a frequency, so the driver falls back to its per-mode default — which on
 * Win9x/2000 is typically 60Hz regardless of what the user had configured.
 * We sample the desktop's configured rate before SDL touches the display
 * (via ENUM_REGISTRY_SETTINGS so we read the persisted rate rather than
 * whatever happens to be active), then after SDL_SetVideoMode we re-apply
 * that rate on top of SDL's chosen resolution. No-op on non-Win32. */
#ifdef _WIN32
#include <windows.h>
static DWORD g_savedDesktopHz = 0;

static void saveDesktopRefreshHz(void)
{
    DEVMODE dm;
    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettings(NULL, ENUM_REGISTRY_SETTINGS, &dm)) {
        g_savedDesktopHz = dm.dmDisplayFrequency;
        printf("Display: desktop refresh rate is %lu Hz\n",
               (unsigned long)g_savedDesktopHz);
    } else {
        fprintf(stderr, "Display: EnumDisplaySettings failed\n");
    }
}

static void applyFullscreenRefreshHz(int width, int height)
{
    /* 0 = not queried yet, 1 = "default" hardware rate — skip in either case
       (setting to 1 would force the driver back to the very thing we're
       trying to override). */
    if (g_savedDesktopHz <= 1) return;

    DEVMODE dm;
    memset(&dm, 0, sizeof(dm));
    dm.dmSize             = sizeof(dm);
    dm.dmPelsWidth        = width;
    dm.dmPelsHeight       = height;
    dm.dmDisplayFrequency = g_savedDesktopHz;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;

    LONG rc = ChangeDisplaySettingsEx(NULL, &dm, NULL, CDS_FULLSCREEN, NULL);
    if (rc == DISP_CHANGE_SUCCESSFUL) {
        printf("Display: refresh set to %lu Hz (fullscreen %dx%d)\n",
               (unsigned long)g_savedDesktopHz, width, height);
    } else {
        fprintf(stderr,
                "Display: could not force %lu Hz at %dx%d (rc=%ld); "
                "staying at SDL's default rate\n",
                (unsigned long)g_savedDesktopHz, width, height, (long)rc);
    }
}
#else
static void saveDesktopRefreshHz(void) {}
static void applyFullscreenRefreshHz(int, int) {}
#endif

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
        GLuint diffTex = texCacheGet(cache, "assets/levels/diffuse.bmp", GL_CLAMP_TO_EDGE);
        GLuint lmTex  = texCacheGet(cache, "assets/levels/lightmap.bmp", GL_CLAMP_TO_EDGE);
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
        } else {
            /* Blocked — print once per activation session so the user
               knows the sweep is rejecting. Usually means the collider
               AABB intersects world geometry (wall, floor, another
               decoration). Press B to see the wireframe. */
            static int lastLoggedEnt = -1;
            if (lastLoggedEnt != i) {
                printf("door: '%s' blocked (sweep hit — collider overlapping "
                       "world geometry?)\n", e->name);
                lastLoggedEnt = i;
            }
        }
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

    /* Sample the configured desktop refresh rate BEFORE SDL touches the
       display, so later we can reapply it over SDL's fullscreen mode
       (which otherwise drops to the driver's 60Hz default on Win9x). */
    saveDesktopRefreshHz();

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

    SoundLibrary sndLib;
    sndLibInit(&sndLib);
    /* Sounds are registered from assets.lua once the Lua runtime boots
       (further down, after UI + entities exist). sndLib is empty until
       then — nothing tries to play before the main loop. */

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

    /* Re-apply the desktop refresh rate on top of SDL's fullscreen mode. */
    if (fullscreen) applyFullscreenRefreshHz(SCREEN_W, SCREEN_H);

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

    /* App-level Lua runtime + asset registry. Loaded once before any game
       boots so .ent files can resolve model/texture logical names and
       sound/font tables can populate their libraries. scriptInit takes
       NULL entities — gameInit rebinds script->entities for the current
       session. */
    AssetRegistry assetReg;
    assetRegInit(&assetReg);
    ScriptSystem script;
    scriptInit(&script, &ui, &snd, &sndLib, NULL, &assetReg);
    scriptLoadAssets(&script, "assets.lua");

    /* App state machine. Starts in MODE_MENU with the MainMenu pushed and
       no Game yet — New Game triggers the first gameInit. */
    AppState app;
    appInit(&app, SCREEN_W, SCREEN_H, &ui, &assetReg, &script, &snd, &sndLib);

    /* Game struct is allocated once on the stack; gameInited tracks
       whether it currently holds an active session (so gameFree only
       runs on one that was initialized). */
    Game game;
    int  gameInited = 0;

    /* Start in menu mode: release the mouse so the cursor can roam the UI. */
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    SDL_ShowCursor(SDL_ENABLE);

    const float moveSpeed = 6.0f;

    Uint32 lastTime = SDL_GetTicks();
    SDL_Event event;

    while (app.running) {
        Uint32 now = SDL_GetTicks();
        float dt = (now - lastTime) / 1000.0f;
        if (dt > 0.15f) dt = 0.15f;
        lastTime = now;

        uiUpdateMessage(&ui, dt);

        if (app.mode == MODE_MENU) {
            menuTick(&app, dt);
        } else {
        /* ---- MODE_GAME: gameplay event handling + simulation + render ---- */
        game.fpsAccum += dt;
        game.fpsFrames++;
        if (game.fpsAccum >= 0.5f) {
            game.fpsDisplay = (int)(game.fpsFrames / game.fpsAccum + 0.5f);
            game.fpsAccum = 0.0f;
            game.fpsFrames = 0;
        }

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                app.running = 0;
                break;
            }
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    /* Back to the menu — game state is preserved so the
                       player can Continue. Next frame takes the MODE_MENU
                       branch; break out of the event queue so remaining
                       in-game events don't fire after the transition. */
                    app.mode = MODE_MENU;
                    appEnterMenu(&app);
                    SDL_WM_GrabInput(SDL_GRAB_OFF);
                    SDL_ShowCursor(SDL_ENABLE);
                    break;
                }
                if (event.key.keysym.sym == SDLK_f) {
                    game.flashlightOn = !game.flashlightOn;
                    printf("Flashlight: %s\n", game.flashlightOn ? "ON" : "OFF");
                }
                if (event.key.keysym.sym == SDLK_b) {
                    game.debugColliders = !game.debugColliders;
                    printf("Collider wireframes: %s\n", game.debugColliders ? "ON" : "OFF");
                }
                if (event.key.keysym.sym == SDLK_n) {
                    game.debugNav = !game.debugNav;
                    printf("Nav graph: %s (%d node%s)\n",
                           game.debugNav ? "ON" : "OFF",
                           game.nav.numNodes, game.nav.numNodes == 1 ? "" : "s");
                    /* One-shot A* test from player to the last waypoint so the
                       pathfinder has been exercised before any AI uses it. */
                    if (game.debugNav && game.nav.numNodes >= 2) {
                        float px, py, pz;
                        physGetPlayerPos(&game.phys, &px, &py, &pz);
                        Vec3 from; from.x = px; from.y = py; from.z = pz;
                        Vec3 to = game.nav.nodes[game.nav.numNodes - 1];
                        int path[NAV_MAX_NODES];
                        int len = navFindPath(&game.nav, &game.phys, from, to,
                                              path, NAV_MAX_NODES);
                        printf("nav: test path (player -> node %d): %d step(s)",
                               game.nav.numNodes - 1, len);
                        for (int i = 0; i < len; i++) printf(" %d", path[i]);
                        printf("\n");
                    }
                }
                if (event.key.keysym.sym == SDLK_p) {
                    float _px, _py, _pz;
                    physGetPlayerPos(&game.phys, &_px, &_py, &_pz);
                    printf("player pos: X=%.3f Y=%.3f Z=%.3f  yaw=%.1f pitch=%.1f\n",
                           _px, _py, _pz, game.yaw, game.pitch);
                }
                if (event.key.keysym.sym == SDLK_SPACE) {
                    if (game.phys.character->onGround()) {
                        game.phys.character->jump();
                        sndPlay(&snd, sndLibFind(&sndLib, "jump"));
                    }
                }
            }
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    game.gunFlashTimer = 4;
                    sndPlay(&snd, sndLibFind(&sndLib, "fire"));
                }
            }
            if (event.type == SDL_MOUSEMOTION) {
                game.yaw   -= event.motion.xrel * 0.15f;
                game.pitch -= event.motion.yrel * 0.15f;
                if (game.pitch > 89.0f) game.pitch = 89.0f;
                if (game.pitch < -89.0f) game.pitch = -89.0f;
            }
        }

        /* WASD movement */
        Uint8 *keys = SDL_GetKeyState(NULL);
        float forwardX = -sinf(game.yaw * (float)M_PI / 180.0f);
        float forwardZ = -cosf(game.yaw * (float)M_PI / 180.0f);
        float rightX = cosf(game.yaw * (float)M_PI / 180.0f);
        float rightZ = -sinf(game.yaw * (float)M_PI / 180.0f);

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
        const float ACCEL    = 10.0f;   /* m/s²-ish pull toward wish velocity */
        const float FRICTION = 20.0f;   /* m/s²-ish slowdown when no input    */
        if (wishLen > 0.001f) {
            game.velX += (wishX - game.velX) * ACCEL * dt;
            game.velZ += (wishZ - game.velZ) * ACCEL * dt;
        } else {
            float speed = sqrtf(game.velX * game.velX + game.velZ * game.velZ);
            if (speed > 0.001f) {
                float drop = FRICTION * dt;
                float newSpeed = speed - drop;
                if (newSpeed < 0) newSpeed = 0;
                game.velX *= newSpeed / speed;
                game.velZ *= newSpeed / speed;
            } else {
                game.velX = 0; game.velZ = 0;
            }
        }
        int isMoving = (game.velX * game.velX + game.velZ * game.velZ) > 0.04f;

        /* Convert velocity (m/s) to per-substep displacement for Bullet. */
        const float perStep = 1.0f / 120.0f;
        game.phys.character->setWalkDirection(
            btVector3(game.velX * perStep, 0, game.velZ * perStep));

        if (isMoving && game.phys.character->onGround()) {
            game.footstepTimer -= (int)(dt * 1000);
            if (game.footstepTimer <= 0) {
                sndPlay(&snd, sndLibFind(&sndLib, "step"));
                game.footstepTimer = 400;
            }
        } else {
            game.footstepTimer = 0;
        }

        physStep(&game.phys, dt);

        float px, py, pz;
        physGetPlayerPos(&game.phys, &px, &py, &pz);
        /* Capsule center is 0.875m above the floor when grounded (halfHeight
           0.525 + radius 0.35), so +0.755 puts the camera at 1.63m — realistic
           eye height for a 1.75m-tall person. */
        float eyeY = py + 0.765f;

        float lookX = px + forwardX;
        float lookY = eyeY + sinf(game.pitch * (float)M_PI / 180.0f);
        float lookZ = pz + forwardZ;

        if (game.gunFlashTimer > 0) game.gunFlashTimer--;

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
            float strafeVel = game.velX * rightX + game.velZ * rightZ;
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
        if (game.flashlightOn && game.hasDynLm) {
            float dirX = lookX - px;
            float dirY = lookY - eyeY;
            float dirZ = lookZ - pz;
            float dirLen = sqrtf(dirX*dirX + dirY*dirY + dirZ*dirZ);
            if (dirLen > 0.0001f) { dirX /= dirLen; dirY /= dirLen; dirZ /= dirLen; }

            float hitX, hitY, hitZ;
            if (physRaycast(&game.phys, px, eyeY, pz, dirX, dirY, dirZ, 30.0f,
                            &hitX, &hitY, &hitZ)) {
                dynLmUpdate(&game.dynLm, hitX, hitY, hitZ,
                            3.0f, 1.0f, 1.0f, 0.95f, 0.8f);
            } else {
                dynLmRestore(&game.dynLm);
            }
        } else if (game.hasDynLm) {
            dynLmRestore(&game.dynLm);
        }

        renderLevelSectored(&game.level, &game.texCache);

        /* Update and render entities */
        entUpdate(game.entities, px, py, pz, dt);
        updateDoors(game.entities, &game.phys, dt);
        glEnable(GL_LIGHTING);
        glColor3f(1.0f, 1.0f, 1.0f);
        entRender(game.entities);

        if (game.debugColliders) physDebugDrawColliders(&game.phys);
        if (game.debugNav) navDebugRender(&game.nav);

        renderGun(game.gunFlashTimer);
        renderCrosshair();

        /* HUD — placeholder values until player/weapon state exists.
           Coordinates are virtual (1080-tall canvas, origin at center). */
        uiBegin(&ui);
        {
            const float pad = 30.0f;
            const float barW = 400.0f, barH = 40.0f;
            const float halfW = uiGetWidth(&ui)  * 0.5f;
            const float halfH = uiGetHeight(&ui) * 0.5f;
            UiColor white = uiRgb(1, 1, 1);
            UiColor red   = uiRgb(0.85f, 0.2f, 0.2f);
            UiColor amber = uiRgb(1.0f, 1.0f, 0.2f);

            /* Health: bottom-left */
            uiText(&ui, -halfW + pad, halfH - pad - barH - 34,
                   white, "HEALTH", 2.5f);
            uiBar(uiRectMake(-halfW + pad, halfH - pad - barH, barW, barH),
                  0.87f, red);
            uiText(&ui, -halfW + pad + barW + 20, halfH - pad - barH + 4,
                   white, "87", 3.5f);

            /* Ammo: bottom-right */
            char ammo[32];
            snprintf(ammo, sizeof(ammo), "%d / %d", 24, 72);
            uiText(&ui, halfW - pad, halfH - pad - 40, amber, ammo,
                   4.0f, UI_ALIGN_TOP | UI_ALIGN_RIGHT);

            /* FPS: top-right */
            char fps[32];
            snprintf(fps, sizeof(fps), "%d FPS", game.fpsDisplay);
            uiText(&ui, halfW - pad, -halfH + pad, white, fps,
                   2.0f, UI_ALIGN_TOP | UI_ALIGN_RIGHT);

            /* Transient script-driven message (ui_show_message from Lua). */
            uiDrawMessage(&ui);
        }
        uiEnd(&ui);
        }  /* end MODE_GAME branch */

        /* Process side-effect actions surfaced by the menu or game loop. */
        switch (app.pendingAction) {
            case PENDING_NEW_GAME:
                /* Paint a single "LOADING" frame before gameInit blocks the
                   thread, so the window doesn't sit on a stale menu frame
                   (or black) while the level and physics come up. */
                drawLoadingScreen(&app);
                if (gameInited) {
                    gameFree(&game, &script);
                    gameInited = 0;
                    app.game = NULL;
                }
                if (!gameInit(&game, "assets/levels/test_level.obj",
                              "assets/levels/test_level.ent", &script, &assetReg)) {
                    fprintf(stderr, "gameInit failed\n");
                    app.running = 0;
                } else {
                    gameInited = 1;
                    app.game = &game;
                    app.mode = MODE_GAME;
                    screenStackClear(&app.screens);
                    SDL_WM_GrabInput(SDL_GRAB_ON);
                    SDL_ShowCursor(SDL_DISABLE);
                }
                break;
            case PENDING_CONTINUE:
                if (gameInited) {
                    app.mode = MODE_GAME;
                    screenStackClear(&app.screens);
                    SDL_WM_GrabInput(SDL_GRAB_ON);
                    SDL_ShowCursor(SDL_DISABLE);
                }
                break;
            case PENDING_QUIT:
                app.running = 0;
                break;
        }
        app.pendingAction = PENDING_NONE;

        SDL_GL_SwapBuffers();
        SDL_Delay(1);
    }

    /* Cleanup */
    if (gameInited) gameFree(&game, &script);
    appShutdown(&app);
    scriptShutdown(&script);
    uiShutdown(&ui);

    sndLibShutdown(&sndLib);
    sndShutdown(&snd);
    SDL_Quit();
    return 0;
}
