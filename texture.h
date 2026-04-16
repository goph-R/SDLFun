#ifndef TEXTURE_H
#define TEXTURE_H

#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

/* BMP file header structures (packed) */
#pragma pack(push, 1)
struct BmpFileHeader {
    unsigned short type;
    unsigned int size;
    unsigned short reserved1;
    unsigned short reserved2;
    unsigned int offsetData;
};

struct BmpInfoHeader {
    unsigned int size;
    int width;
    int height;
    unsigned short planes;
    unsigned short bitCount;
    unsigned int compression;
    unsigned int sizeImage;
    int xPelsPerMeter;
    int yPelsPerMeter;
    unsigned int clrUsed;
    unsigned int clrImportant;
};
#pragma pack(pop)

/* Upload raw RGB data to an OpenGL texture with specified wrap mode */
static GLuint uploadTexture(unsigned char *rgbData, int width, int height, int wrapMode)
{
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, rgbData);
    return texID;
}

/* Load a 24-bit or 32-bit BMP file into an OpenGL texture.
   Returns the GL texture ID, or 0 on failure. */
static GLuint loadBMPEx(const char *filename, int wrapMode)
{
    FILE *f = fopen(filename, "rb");
    if (!f) return 0;

    BmpFileHeader fh;
    BmpInfoHeader ih;
    fread(&fh, sizeof(fh), 1, f);
    fread(&ih, sizeof(ih), 1, f);

    if (fh.type != 0x4D42) { /* 'BM' */
        fprintf(stderr, "texture: %s is not a BMP file\n", filename);
        fclose(f);
        return 0;
    }

    if (ih.bitCount != 24 && ih.bitCount != 32) {
        fprintf(stderr, "texture: %s is %d-bit, need 24 or 32\n", filename, ih.bitCount);
        fclose(f);
        return 0;
    }

    if (ih.compression != 0) {
        fprintf(stderr, "texture: %s is compressed, not supported\n", filename);
        fclose(f);
        return 0;
    }

    int width = ih.width;
    int height = ih.height < 0 ? -ih.height : ih.height;
    int topDown = ih.height < 0;
    int bpp = ih.bitCount / 8;
    int rowSize = (width * bpp + 3) & ~3; /* rows are padded to 4 bytes */

    unsigned char *rawData = (unsigned char *)malloc(rowSize * height);
    fseek(f, fh.offsetData, SEEK_SET);
    fread(rawData, 1, rowSize * height, f);
    fclose(f);

    /* Convert to RGB (OpenGL wants top-to-bottom, RGB order) */
    unsigned char *rgbData = (unsigned char *)malloc(width * height * 3);

    for (int y = 0; y < height; y++) {
        int srcY = topDown ? y : (height - 1 - y);
        unsigned char *src = rawData + srcY * rowSize;
        unsigned char *dst = rgbData + y * width * 3;
        for (int x = 0; x < width; x++) {
            /* BMP stores BGR */
            dst[x * 3 + 0] = src[x * bpp + 2]; /* R */
            dst[x * 3 + 1] = src[x * bpp + 1]; /* G */
            dst[x * 3 + 2] = src[x * bpp + 0]; /* B */
        }
    }
    free(rawData);

    GLuint texID = uploadTexture(rgbData, width, height, wrapMode);
    free(rgbData);

    printf("texture: loaded %s (%dx%d)\n", filename, width, height);
    return texID;
}

/* Load a TGA file (uncompressed, 24 or 32 bit) */
static GLuint loadTGAEx(const char *filename, int wrapMode)
{
    FILE *f = fopen(filename, "rb");
    if (!f) return 0;

    unsigned char header[18];
    fread(header, 1, 18, f);

    int width = header[12] | (header[13] << 8);
    int height = header[14] | (header[15] << 8);
    int bpp = header[16] / 8;

    if (bpp != 3 && bpp != 4) {
        fprintf(stderr, "texture: %s is %d-bit, need 24 or 32\n", filename, bpp * 8);
        fclose(f);
        return 0;
    }

    if (header[2] != 2) { /* uncompressed true-color only */
        fprintf(stderr, "texture: %s is not uncompressed true-color TGA\n", filename);
        fclose(f);
        return 0;
    }

    /* Skip image ID */
    if (header[0] > 0) fseek(f, header[0], SEEK_CUR);

    int dataSize = width * height * bpp;
    unsigned char *rawData = (unsigned char *)malloc(dataSize);
    fread(rawData, 1, dataSize, f);
    fclose(f);

    /* TGA is BGR(A), bottom-up by default. Convert to RGB. */
    unsigned char *rgbData = (unsigned char *)malloc(width * height * 3);
    int topDown = (header[17] & 0x20) != 0;

    for (int y = 0; y < height; y++) {
        int srcY = topDown ? y : (height - 1 - y);
        unsigned char *src = rawData + srcY * width * bpp;
        unsigned char *dst = rgbData + y * width * 3;
        for (int x = 0; x < width; x++) {
            dst[x * 3 + 0] = src[x * bpp + 2];
            dst[x * 3 + 1] = src[x * bpp + 1];
            dst[x * 3 + 2] = src[x * bpp + 0];
        }
    }
    free(rawData);

    GLuint texID = uploadTexture(rgbData, width, height, wrapMode);
    free(rgbData);

    printf("texture: loaded %s (%dx%d)\n", filename, width, height);
    return texID;
}

/* Auto-detect format by extension, with wrap mode */
static GLuint loadTextureEx(const char *filename, int wrapMode)
{
    int len = strlen(filename);
    if (len > 4 && (strcmp(filename + len - 4, ".bmp") == 0 ||
                    strcmp(filename + len - 4, ".BMP") == 0)) {
        return loadBMPEx(filename, wrapMode);
    }
    if (len > 4 && (strcmp(filename + len - 4, ".tga") == 0 ||
                    strcmp(filename + len - 4, ".TGA") == 0)) {
        return loadTGAEx(filename, wrapMode);
    }
    fprintf(stderr, "texture: unknown format for %s (use .bmp or .tga)\n", filename);
    return 0;
}

/* Backward-compatible wrapper: loads with GL_CLAMP_TO_EDGE */
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
};

struct TexCache {
    TexCacheEntry entries[TEX_CACHE_MAX];
    int count;
};

static void texCacheInit(TexCache *tc)
{
    tc->count = 0;
}

/* Get or load a texture. Returns GL texture ID, or 0 if file not found. */
static GLuint texCacheGet(TexCache *tc, const char *path, int wrapMode)
{
    if (!path || !path[0]) return 0;

    /* Check cache (path + wrapMode must both match) */
    for (int i = 0; i < tc->count; i++) {
        if (tc->entries[i].wrapMode == wrapMode &&
            strcmp(tc->entries[i].path, path) == 0) {
            return tc->entries[i].texID;
        }
    }

    /* Load new */
    GLuint tex = loadTextureEx(path, wrapMode);
    if (tex && tc->count < TEX_CACHE_MAX) {
        strncpy(tc->entries[tc->count].path, path, 127);
        tc->entries[tc->count].path[127] = '\0';
        tc->entries[tc->count].texID = tex;
        tc->entries[tc->count].wrapMode = wrapMode;
        tc->count++;
    }
    return tex;
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
