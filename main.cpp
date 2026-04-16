#ifdef _WIN32
#include <SDL/SDL.h>
#else
#include <SDL.h>
#endif
#include <GL/gl.h>
#ifdef _WIN32
#include <fmod/fmod.h>
#else
#include <fmod.h>
#endif
#include <cstdlib>
#include <cstdio>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "obj_loader.h"
#include "texture.h"
#include "physics.h"

#define SAMPLE_RATE 44100
#define SCREEN_W 640
#define SCREEN_H 480

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

/* ---- Sound helpers ---- */

FSOUND_SAMPLE *createTone(int index, float freq, int length)
{
    FSOUND_SAMPLE *sample = FSOUND_Sample_Alloc(index, length,
        FSOUND_16BITS | FSOUND_SIGNED | FSOUND_MONO | FSOUND_LOOP_OFF,
        SAMPLE_RATE, 255, 128, 128);
    if (!sample) return NULL;

    void *ptr1, *ptr2;
    unsigned int len1, len2;
    if (FSOUND_Sample_Lock(sample, 0, length * 2, &ptr1, &ptr2, &len1, &len2)) {
        short *buf = (short *)ptr1;
        int numSamples = len1 / 2;
        for (int i = 0; i < numSamples; i++) {
            float t = (float)i / SAMPLE_RATE;
            float envelope = 1.0f - (float)i / numSamples;
            buf[i] = (short)(sinf(2.0f * (float)M_PI * freq * t) * 32000.0f * envelope);
        }
        FSOUND_Sample_Unlock(sample, ptr1, ptr2, len1, len2);
    }
    return sample;
}

FSOUND_SAMPLE *createGunshot(int index)
{
    int length = 4410;
    FSOUND_SAMPLE *sample = FSOUND_Sample_Alloc(index, length,
        FSOUND_16BITS | FSOUND_SIGNED | FSOUND_MONO | FSOUND_LOOP_OFF,
        SAMPLE_RATE, 255, 128, 128);
    if (!sample) return NULL;

    void *ptr1, *ptr2;
    unsigned int len1, len2;
    if (FSOUND_Sample_Lock(sample, 0, length * 2, &ptr1, &ptr2, &len1, &len2)) {
        short *buf = (short *)ptr1;
        int numSamples = len1 / 2;
        for (int i = 0; i < numSamples; i++) {
            float envelope = 1.0f - (float)i / numSamples;
            envelope *= envelope;
            float noise = (float)(rand() % 65536 - 32768) / 32768.0f;
            buf[i] = (short)(noise * 28000.0f * envelope);
        }
        FSOUND_Sample_Unlock(sample, ptr1, ptr2, len1, len2);
    }
    return sample;
}

FSOUND_SAMPLE *createFootstep(int index)
{
    int length = 2205;
    FSOUND_SAMPLE *sample = FSOUND_Sample_Alloc(index, length,
        FSOUND_16BITS | FSOUND_SIGNED | FSOUND_MONO | FSOUND_LOOP_OFF,
        SAMPLE_RATE, 200, 128, 128);
    if (!sample) return NULL;

    void *ptr1, *ptr2;
    unsigned int len1, len2;
    if (FSOUND_Sample_Lock(sample, 0, length * 2, &ptr1, &ptr2, &len1, &len2)) {
        short *buf = (short *)ptr1;
        int numSamples = len1 / 2;
        for (int i = 0; i < numSamples; i++) {
            float envelope = 1.0f - (float)i / numSamples;
            float thud = sinf(2.0f * (float)M_PI * 80.0f * (float)i / SAMPLE_RATE);
            float noise = (float)(rand() % 65536 - 32768) / 32768.0f;
            buf[i] = (short)((thud * 0.7f + noise * 0.3f) * 20000.0f * envelope);
        }
        FSOUND_Sample_Unlock(sample, ptr1, ptr2, len1, len2);
    }
    return sample;
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
    }
    if (hasDiffuse) {
        glDisable(GL_TEXTURE_2D);
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

/* ---- Main ---- */

int main(int argc, char *argv[])
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    if (!FSOUND_Init(SAMPLE_RATE, 32, 0)) {
        fprintf(stderr, "FMOD init failed\n");
        SDL_Quit();
        return 1;
    }

    FSOUND_SAMPLE *sndGunshot = createGunshot(FSOUND_FREE);
    FSOUND_SAMPLE *sndFootstep = createFootstep(FSOUND_FREE);
    FSOUND_SAMPLE *sndJump = createTone(FSOUND_FREE, 220.0f, 4410);

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Surface *screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 32, SDL_OPENGL);
    if (!screen) {
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        FSOUND_Close();
        SDL_Quit();
        return 1;
    }

    SDL_WM_SetCaption("FPS Demo - SDL + OpenGL + Bullet + FMOD", NULL);
    SDL_WM_GrabInput(SDL_GRAB_ON);
    SDL_ShowCursor(SDL_DISABLE);

    /* OpenGL setup */
    glViewport(0, 0, SCREEN_W, SCREEN_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glSetPerspective(60.0f, (float)SCREEN_W / SCREEN_H, 0.1f, 200.0f);
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

    /* Load textures (optional - falls back to colored rendering) */
    GLuint texDiffuse = loadTexture("diffuse.bmp");
    GLuint texLightmap = loadTexture("lightmap.bmp");

    if (texDiffuse)
        printf("Diffuse texture loaded\n");
    else
        printf("No diffuse.bmp found, using colored rendering\n");

    if (texLightmap)
        printf("Lightmap texture loaded\n");
    else
        printf("No lightmap.bmp found, lightmap disabled\n");

    /* Load level */
    ObjMesh level;
    objInit(&level);
    if (!objLoad(&level, "test_level.obj")) {
        fprintf(stderr, "Failed to load test_level.obj\n");
        FSOUND_Close();
        SDL_Quit();
        return 1;
    }
    printf("Level loaded: %d verts, %d texcoords, %d tris\n",
           level.numVerts, level.numTexcoords, level.numTris);

    /* Init physics */
    PhysWorld phys;
    physInit(&phys);
    physLoadLevel(&phys, &level);
    physCreatePlayer(&phys, 0.0f, 2.0f, 0.0f);

    /* Camera state */
    float yaw = 0.0f;
    float pitch = 0.0f;
    float moveSpeed = 6.0f;

    int gunFlashTimer = 0;
    int footstepTimer = 0;

    Uint32 lastTime = SDL_GetTicks();

    SDL_Event event;
    int running = 1;

    while (running) {
        Uint32 now = SDL_GetTicks();
        float dt = (now - lastTime) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        lastTime = now;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = 0;
                }
                if (event.key.keysym.sym == SDLK_SPACE) {
                    if (phys.character->onGround()) {
                        phys.character->jump();
                        if (sndJump) FSOUND_PlaySound(FSOUND_FREE, sndJump);
                    }
                }
            }
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    gunFlashTimer = 4;
                    if (sndGunshot) FSOUND_PlaySound(FSOUND_FREE, sndGunshot);
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

        float moveX = 0, moveZ = 0;
        if (keys[SDLK_w]) { moveX += forwardX; moveZ += forwardZ; }
        if (keys[SDLK_s]) { moveX -= forwardX; moveZ -= forwardZ; }
        if (keys[SDLK_a]) { moveX -= rightX;   moveZ -= rightZ; }
        if (keys[SDLK_d]) { moveX += rightX;   moveZ += rightZ; }

        float moveLen = sqrtf(moveX * moveX + moveZ * moveZ);
        int isMoving = 0;
        if (moveLen > 0.001f) {
            moveX = (moveX / moveLen) * moveSpeed * dt;
            moveZ = (moveZ / moveLen) * moveSpeed * dt;
            isMoving = 1;
        }

        phys.character->setWalkDirection(btVector3(moveX, 0, moveZ));

        if (isMoving && phys.character->onGround()) {
            footstepTimer -= (int)(dt * 1000);
            if (footstepTimer <= 0) {
                if (sndFootstep) FSOUND_PlaySound(FSOUND_FREE, sndFootstep);
                footstepTimer = 400;
            }
        } else {
            footstepTimer = 0;
        }

        physStep(&phys, dt);

        float px, py, pz;
        physGetPlayerPos(&phys, &px, &py, &pz);
        float eyeY = py + 0.6f;

        float lookX = px + forwardX;
        float lookY = eyeY + sinf(pitch * (float)M_PI / 180.0f);
        float lookZ = pz + forwardZ;

        if (gunFlashTimer > 0) gunFlashTimer--;

        /* ---- Render ---- */
        glClearColor(0.4f, 0.6f, 0.8f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        float lp[] = { 0.0f, 10.0f, 0.0f, 1.0f };
        glLightfv(GL_LIGHT0, GL_POSITION, lp);

        glLoadIdentity();
        glLookAt(px, eyeY, pz, lookX, lookY, lookZ);

        renderLevel(&level, texDiffuse, texLightmap);

        renderGun(gunFlashTimer);
        renderCrosshair();

        SDL_GL_SwapBuffers();
        SDL_Delay(1);
    }

    /* Cleanup */
    if (texDiffuse) glDeleteTextures(1, &texDiffuse);
    if (texLightmap) glDeleteTextures(1, &texLightmap);
    physCleanup(&phys);
    objFree(&level);

    if (sndGunshot) FSOUND_Sample_Free(sndGunshot);
    if (sndFootstep) FSOUND_Sample_Free(sndFootstep);
    if (sndJump) FSOUND_Sample_Free(sndJump);
    FSOUND_Close();
    SDL_Quit();
    return 0;
}
