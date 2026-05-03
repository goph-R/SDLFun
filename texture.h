#ifndef TEXTURE_H
#define TEXTURE_H

#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_THREAD_SAFE
#define STBI_NO_THREAD_LOCALS
#include "vendor/stb/stb_image.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

/* Upload raw RGB or RGBA pixel data to an OpenGL texture.
   channels must be 3 (RGB) or 4 (RGBA). */
static GLuint uploadTextureN(unsigned char *pixelData, int width, int height,
                             int wrapMode, int channels)
{
    GLuint texID;
    GLenum fmt = (channels == 4) ? GL_RGBA : GL_RGB;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, width, height, 0,
                 fmt, GL_UNSIGNED_BYTE, pixelData);
    return texID;
}

static GLuint uploadTexture(unsigned char *rgbData, int width, int height, int wrapMode)
{
    return uploadTextureN(rgbData, width, height, wrapMode, 3);
}

/* Load a PNG into an OpenGL texture. Only PNG is supported since the
   asset pipeline migrated away from BMP/TGA. stb_image decodes directly
   into the layout we need (top-down, 8-bit per channel).

   keepAlpha=0 forces RGB (alpha dropped even if source has it).
   keepAlpha=1 forces RGBA (alpha filled with 255 if source is RGB).
   Returns the GL texture ID, or 0 on failure. */
static GLuint loadTextureExA(const char *filename, int wrapMode, int keepAlpha)
{
    int w = 0, h = 0, srcCh = 0;
    int desired = keepAlpha ? 4 : 3;
    unsigned char *pix = stbi_load(filename, &w, &h, &srcCh, desired);
    if (!pix) {
        conLogf("texture: cannot load %s (%s)\n", filename, stbi_failure_reason());
        return 0;
    }
    GLuint texID = uploadTextureN(pix, w, h, wrapMode, desired);
    stbi_image_free(pix);
    conLogf("texture: loaded %s (%dx%d, %d-ch)\n", filename, w, h, desired);
    return texID;
}

static GLuint loadTextureEx(const char *filename, int wrapMode)
{
    return loadTextureExA(filename, wrapMode, 0);
}

/* Backward-compatible wrapper: loads with GL_CLAMP_TO_EDGE. */
static GLuint loadTexture(const char *filename)
{
    return loadTextureEx(filename, GL_CLAMP_TO_EDGE);
}

/* ---- Texture Cache ---- */

#define TEX_CACHE_MAX 64

struct TexCacheEntry {
    char path[128];
    GLuint texID;
    int wrapMode;
    int keepAlpha;
};

struct TexCache {
    TexCacheEntry entries[TEX_CACHE_MAX];
    int count;
};

static void texCacheInit(TexCache *tc)
{
    tc->count = 0;
}

/* Get or load a texture. Returns GL texture ID, or 0 if file not found.
   keepAlpha=1 uploads the texture as RGBA (for alpha-test materials or
   RGBA overlays). The flag is part of the cache key so the same file
   can coexist as RGB and RGBA if a caller ever needs both. */
static GLuint texCacheGetA(TexCache *tc, const char *path, int wrapMode, int keepAlpha)
{
    if (!path || !path[0]) return 0;

    for (int i = 0; i < tc->count; i++) {
        if (tc->entries[i].wrapMode == wrapMode &&
            tc->entries[i].keepAlpha == keepAlpha &&
            strcmp(tc->entries[i].path, path) == 0) {
            return tc->entries[i].texID;
        }
    }

    GLuint tex = loadTextureExA(path, wrapMode, keepAlpha);
    if (tex && tc->count < TEX_CACHE_MAX) {
        strncpy(tc->entries[tc->count].path, path, 127);
        tc->entries[tc->count].path[127] = '\0';
        tc->entries[tc->count].texID = tex;
        tc->entries[tc->count].wrapMode = wrapMode;
        tc->entries[tc->count].keepAlpha = keepAlpha;
        tc->count++;
    }
    return tex;
}

static GLuint texCacheGet(TexCache *tc, const char *path, int wrapMode)
{
    return texCacheGetA(tc, path, wrapMode, 0);
}

static void texCacheFree(TexCache *tc)
{
    for (int i = 0; i < tc->count; i++) {
        if (tc->entries[i].texID)
            glDeleteTextures(1, &tc->entries[i].texID);
    }
    tc->count = 0;
}

#endif
