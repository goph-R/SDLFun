#ifndef RENDER_LEVEL_H
#define RENDER_LEVEL_H

/* ---- Level render module (header-only, static) ----
 *
 * Extracted from main.cpp so both the game (main.cpp) and the SOOB level
 * editor can draw the lit level through the EXACT same code path. This is
 * pure fixed-function GL 1.1 + GL_ARB_multitexture — no SDL, no windowing,
 * no game loop. The caller owns the GL context (SDL in the game, an FLTK
 * Fl_Gl_Window in the editor) and supplies the camera.
 *
 * Include order (matches the project's header-only convention — every module
 * assumes its dependencies are already included in the single TU):
 *   - obj_loader.h  (Vec2, Vec3, ObjMesh, Material, Sector, Triangle)
 *   - texture.h     (TexCache, texCacheGet / texCacheGetA)
 *   - a forward-declared conLogf(const char*, ...)
 * Include this header AFTER those.
 *
 * On Win98/old MinGW <GL/gl.h> must have been pulled in (with windows.h
 * ahead of it, for APIENTRY) before this header — as it is in main.cpp.
 */

#include <GL/gl.h>
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

/* Runtime GL proc-address loader, supplied by the caller so this module has
   no window-system dependency: the game passes SDL_GL_GetProcAddress, the
   FLTK editor passes a wglGetProcAddress wrapper. Unused on Linux, where the
   ARB entry points are linked directly. */
typedef void *(*GLProcLoader)(const char *name);

static void initMultitexture(GLProcLoader getProc)
{
#ifdef _WIN32
    const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
    if (extensions && strstr(extensions, "GL_ARB_multitexture") && getProc) {
        p_MT_ActiveTexture = (PFN_MT_ActiveTexture)getProc("glActiveTextureARB");
        p_MT_MultiTexCoord2f = (PFN_MT_MultiTexCoord2f)getProc("glMultiTexCoord2fARB");
        if (p_MT_ActiveTexture && p_MT_MultiTexCoord2f) {
            hasMultitexture = 1;
            conLogf("Multitexture: supported\n");
        }
    }
#else
    /* Modern Linux GL always has multitexture */
    (void)getProc;
    hasMultitexture = 1;
    conLogf("Multitexture: supported\n");
#endif
    if (!hasMultitexture) {
        conLogf("Multitexture: not available (lightmaps disabled)\n");
    }
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

static void glLookAt(Vec3 eye, Vec3 at)
{
    float fx = at.x - eye.x;
    float fy = at.y - eye.y;
    float fz = at.z - eye.z;
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
    glTranslatef(-eye.x, -eye.y, -eye.z);
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
        GLuint diffTex = texCacheGet(cache, "assets/levels/diffuse.png", GL_CLAMP_TO_EDGE);
        GLuint lmTex  = texCacheGet(cache, "assets/levels/lightmap.png", GL_CLAMP_TO_EDGE);
        renderLevel(mesh, diffTex, lmTex);
        return;
    }

    for (int s = 0; s < mesh->numSectors; s++) {
        Sector *sec = &mesh->sectors[s];
        Material *mat = NULL;
        GLuint diffTex = 0;
        GLuint lmTex = 0;
        float tileScale = 1.0f;
        int alphaTest = 0;
        float alphaRef = 0.5f;

        if (sec->materialId >= 0 && sec->materialId < mesh->numMaterials) {
            mat = &mesh->materials[sec->materialId];
            alphaTest = mat->alphaTest;
            alphaRef  = mat->alphaRef;
            diffTex = texCacheGetA(cache, mat->diffusePath, GL_REPEAT, alphaTest, NULL, NULL);
            lmTex   = texCacheGet(cache, mat->lightmapPath, GL_CLAMP_TO_EDGE);
            tileScale = mat->tilingScale;
        }

        float tileOffU = mat ? mat->tilingOffsetX : 0.0f;
        float tileOffV = mat ? mat->tilingOffsetY : 0.0f;
        int hasDiffuse = (diffTex != 0);
        int hasLM = (lmTex != 0 && hasMultitexture && mesh->numTexcoords > 0);

        if (alphaTest) {
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, alphaRef);
        }

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
        if (alphaTest) {
            glDisable(GL_ALPHA_TEST);
        }
    }
}

#endif /* RENDER_LEVEL_H */
