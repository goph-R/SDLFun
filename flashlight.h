#ifndef FLASHLIGHT_H
#define FLASHLIGHT_H

/*
 * Dynamic Lightmap Flashlight (Half-Life 1 style)
 * ================================================
 *
 * Instead of using GL hardware lights (which are per-vertex and look coarse),
 * this system modifies the lightmap texture pixels directly on the CPU each
 * frame, then re-uploads to GL. This gives per-texel flashlight resolution
 * at the lightmap's pixel density — exactly how GoldSrc (HL1) did it.
 *
 * How it works:
 *
 * 1. LOAD TIME — buildWorldPosMap():
 *    For each triangle in the level mesh, we know its 3 vertices (world XYZ)
 *    and its 3 lightmap UV coordinates (0..1). We rasterize each triangle in
 *    UV space (scaled to lightmap pixel dimensions) and for each covered texel,
 *    we store the interpolated world position. This creates a lookup table:
 *    worldPosMap[texelY][texelX] = (worldX, worldY, worldZ).
 *    Texels that don't map to any geometry are marked with FLT_MAX.
 *
 * 2. EACH FRAME — dynLmUpdate():
 *    - Copy the original baked lightmap pixels to a working buffer
 *    - Raycast from the camera to find the flashlight hit point on a wall
 *    - For each lightmap texel in a region around the projected hit:
 *      - Look up its world position from worldPosMap
 *      - Compute distance from the flashlight hit point
 *      - If within radius, add warm white light with quadratic falloff
 *      - Clamp the resulting color to 255
 *    - Re-upload the union of this frame's region and the previous frame's
 *      with glTexSubImage2D (partial update via GL_UNPACK_ROW_LENGTH)
 *
 * 3. FLASHLIGHT OFF — dynLmRestore():
 *    Copy the last dirtied region back from bakedPixels and upload just that.
 *
 * Only the region the flashlight actually touched is ever copied or uploaded.
 * Doing the reset and the upload full-size instead costs two 768KB passes per
 * frame at 512x512, which measured 10-20fps on a 350MHz PII / GeForce 4 MX
 * against 40-60fps with the flashlight off. Keep both paths box-bounded.
 *
 * Performance:
 *    - worldPosMap build: ~1ms for a 512x512 lightmap with 500 triangles (one time)
 *    - Per-frame update: ~0.2ms for a 128x128 affected region (P4 class CPU)
 *    - glTexSubImage2D: ~0.1ms for a 128x128 partial upload (any GPU)
 *    - Total per frame: ~0.3ms — trivial
 *
 *    Finding the affected box is a sphere test per acceleration-grid cell
 *    (1024 cells at 512x512, 16KB contiguous) instead of a stride walk over
 *    the 3MB worldPosMap. Do NOT try to predict the box from the previous
 *    frame's: the lightmap is an atlas, so texels adjacent in the world can
 *    live anywhere in UV, and a flashlight sweeping from wall to floor jumps
 *    to an unrelated island.
 *
 * Memory:
 *    - worldPosMap: width * height * 3 * sizeof(float) = 3MB for 512x512
 *    - workingBuffer: width * height * 3 bytes = 768KB for 512x512
 *    - Original lightmap kept in RAM: same 768KB
 *    - Total: ~4.5MB for 512x512 lightmap
 */

#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <math.h>
#include <float.h>

#include "obj_loader.h"

/* ---- Dynamic Lightmap ---- */

/* Texels per side of one acceleration-grid cell. 16 keeps the grid small
   enough to stay in cache (1024 cells at 512x512) while still being fine
   enough that a lit cell is mostly lit texels. */
#define DYNLM_BLOCK 16

struct DynLightmap {
    /* Lightmap dimensions */
    int width, height;

    /* Original baked lightmap pixels (RGB, 3 bytes per texel) */
    unsigned char *bakedPixels;

    /* Working buffer (modified each frame, uploaded to GL) */
    unsigned char *workPixels;

    /* World position per texel (3 floats per texel, FLT_MAX = unmapped) */
    float *worldPosMap;

    /* GL texture ID of the lightmap */
    GLuint texID;

    /* State */
    int dirty;  /* 1 = working buffer was modified, needs restore when off */

    /* Bounding box of the texels the PREVIOUS frame's flashlight touched.
       Only this box needs restoring from bakedPixels, and only its union with
       the current box needs re-uploading -- that is what keeps the per-frame
       cost proportional to the lit region instead of the whole lightmap.
       hasPrev = 0 means nothing is dirty yet. */
    int hasPrev;
    int prevMinX, prevMinY, prevMaxX, prevMaxY;

    /* Coarse acceleration grid, built once from worldPosMap. Each cell covers
       DYNLM_BLOCK x DYNLM_BLOCK texels and stores the world-space bounding
       sphere of the mapped texels inside it, so finding the lit region is a
       sphere test per cell over a small contiguous array rather than a stride
       walk through the 3MB worldPosMap. blockUsed = 0 for cells no triangle
       reached. 16 bytes per cell -- 16KB for a 512x512 lightmap. */
    int blocksX, blocksY;
    float *blockSphere;          /* 4 floats per cell: centre xyz, radius */
    unsigned char *blockUsed;
};

/* ---- UV-space triangle rasterizer (barycentric) ---- */
/* Fills worldPosMap for texels covered by a triangle */

static void rasterTriInUV(DynLightmap *dl,
                          Vec2 t0, Vec2 t1, Vec2 t2,
                          Vec3 w0, Vec3 w1, Vec3 w2)
{
    int w = dl->width, h = dl->height;
    float wx0 = w0.x, wy0 = w0.y, wz0 = w0.z;
    float wx1 = w1.x, wy1 = w1.y, wz1 = w1.z;
    float wx2 = w2.x, wy2 = w2.y, wz2 = w2.z;

    /* Scale UVs to pixel coordinates */
    float px0 = t0.u * w, py0 = t0.v * h;
    float px1 = t1.u * w, py1 = t1.v * h;
    float px2 = t2.u * w, py2 = t2.v * h;

    /* Bounding box in pixel space */
    int minX = (int)px0, maxX = (int)px0;
    int minY = (int)py0, maxY = (int)py0;
    if ((int)px1 < minX) minX = (int)px1; if ((int)px1 > maxX) maxX = (int)px1;
    if ((int)px2 < minX) minX = (int)px2; if ((int)px2 > maxX) maxX = (int)px2;
    if ((int)py1 < minY) minY = (int)py1; if ((int)py1 > maxY) maxY = (int)py1;
    if ((int)py2 < minY) minY = (int)py2; if ((int)py2 > maxY) maxY = (int)py2;

    /* Clamp to texture bounds with 1px margin */
    if (minX < 0) minX = 0; if (maxX >= w) maxX = w - 1;
    if (minY < 0) minY = 0; if (maxY >= h) maxY = h - 1;

    /* Barycentric denominator */
    float denom = (py1 - py2) * (px0 - px2) + (px2 - px1) * (py0 - py2);
    if (denom == 0.0f) return; /* degenerate triangle */
    float invDenom = 1.0f / denom;

    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            float cx = x + 0.5f, cy = y + 0.5f; /* texel center */

            /* Barycentric coordinates */
            float bary0 = ((py1 - py2) * (cx - px2) + (px2 - px1) * (cy - py2)) * invDenom;
            float bary1 = ((py2 - py0) * (cx - px2) + (px0 - px2) * (cy - py2)) * invDenom;
            float bary2 = 1.0f - bary0 - bary1;

            if (bary0 >= -0.01f && bary1 >= -0.01f && bary2 >= -0.01f) {
                int idx = (y * w + x) * 3;
                dl->worldPosMap[idx + 0] = bary0 * wx0 + bary1 * wx1 + bary2 * wx2;
                dl->worldPosMap[idx + 1] = bary0 * wy0 + bary1 * wy1 + bary2 * wy2;
                dl->worldPosMap[idx + 2] = bary0 * wz0 + bary1 * wz1 + bary2 * wz2;
            }
        }
    }
}

/* ---- Build world position map from level mesh ---- */

static void dynLmBuildWorldPosMap(DynLightmap *dl, ObjMesh *mesh)
{
    /* Initialize all texels as unmapped */
    int total = dl->width * dl->height * 3;
    for (int i = 0; i < total; i++)
        dl->worldPosMap[i] = FLT_MAX;

    int rasterized = 0;
    for (int i = 0; i < mesh->numTris; i++) {
        Triangle *t = &mesh->tris[i];

        /* Need valid UVs and vertices */
        if (t->t[0] < 0 || t->t[1] < 0 || t->t[2] < 0) continue;
        if (t->t[0] >= mesh->numTexcoords || t->t[1] >= mesh->numTexcoords ||
            t->t[2] >= mesh->numTexcoords) continue;

        Vec3 *v0 = &mesh->verts[t->v[0]];
        Vec3 *v1 = &mesh->verts[t->v[1]];
        Vec3 *v2 = &mesh->verts[t->v[2]];
        Vec2 *tc0 = &mesh->texcoords[t->t[0]];
        Vec2 *tc1 = &mesh->texcoords[t->t[1]];
        Vec2 *tc2 = &mesh->texcoords[t->t[2]];

        rasterTriInUV(dl, *tc0, *tc1, *tc2, *v0, *v1, *v2);
        rasterized++;
    }
    conLogf("flashlight: rasterized %d triangles into %dx%d worldPosMap\n",
           rasterized, dl->width, dl->height);
}

/* ---- Load lightmap with CPU pixel copy ---- */

/* ---- Build the coarse acceleration grid from worldPosMap ---- */

static void dynLmBuildBlockGrid(DynLightmap *dl)
{
    int w = dl->width, h = dl->height;

    dl->blocksX = (w + DYNLM_BLOCK - 1) / DYNLM_BLOCK;
    dl->blocksY = (h + DYNLM_BLOCK - 1) / DYNLM_BLOCK;

    int cells = dl->blocksX * dl->blocksY;
    dl->blockSphere = (float *)malloc(cells * 4 * sizeof(float));
    dl->blockUsed = (unsigned char *)malloc(cells);

    int mapped = 0;
    for (int by = 0; by < dl->blocksY; by++) {
        for (int bx = 0; bx < dl->blocksX; bx++) {
            int cell = by * dl->blocksX + bx;

            int x0 = bx * DYNLM_BLOCK, x1 = x0 + DYNLM_BLOCK;
            int y0 = by * DYNLM_BLOCK, y1 = y0 + DYNLM_BLOCK;
            if (x1 > w) x1 = w;
            if (y1 > h) y1 = h;

            float minX = FLT_MAX, minY = FLT_MAX, minZ = FLT_MAX;
            float maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;
            int any = 0;

            for (int y = y0; y < y1; y++) {
                for (int x = x0; x < x1; x++) {
                    int idx3 = (y * w + x) * 3;
                    if (dl->worldPosMap[idx3] >= FLT_MAX * 0.5f) continue;
                    float px = dl->worldPosMap[idx3 + 0];
                    float py = dl->worldPosMap[idx3 + 1];
                    float pz = dl->worldPosMap[idx3 + 2];
                    if (px < minX) minX = px;
                    if (py < minY) minY = py;
                    if (pz < minZ) minZ = pz;
                    if (px > maxX) maxX = px;
                    if (py > maxY) maxY = py;
                    if (pz > maxZ) maxZ = pz;
                    any = 1;
                }
            }

            dl->blockUsed[cell] = (unsigned char)any;
            if (any) {
                float cx = (minX + maxX) * 0.5f;
                float cy = (minY + maxY) * 0.5f;
                float cz = (minZ + maxZ) * 0.5f;
                float ex = maxX - cx, ey = maxY - cy, ez = maxZ - cz;
                dl->blockSphere[cell * 4 + 0] = cx;
                dl->blockSphere[cell * 4 + 1] = cy;
                dl->blockSphere[cell * 4 + 2] = cz;
                dl->blockSphere[cell * 4 + 3] = sqrtf(ex * ex + ey * ey + ez * ez);
                mapped++;
            }
        }
    }

    conLogf("flashlight: block grid %dx%d, %d of %d cells mapped\n",
            dl->blocksX, dl->blocksY, mapped, cells);
}

static int dynLmInit(DynLightmap *dl, const char *lmPath, ObjMesh *mesh, GLuint existingTexID)
{
    memset(dl, 0, sizeof(DynLightmap));

    /* Decode the PNG to raw RGB. stb_image returns top-down 8-bit rows,
       which is exactly the layout the flashlight needs. */
    int w = 0, h = 0, srcCh = 0;
    unsigned char *src = stbi_load(lmPath, &w, &h, &srcCh, 3);
    if (!src) {
        conLogf("flashlight: cannot load lightmap %s (%s)\n",
                lmPath, stbi_failure_reason());
        return 0;
    }
    dl->width = w;
    dl->height = h;

    int pixelCount = dl->width * dl->height * 3;
    dl->bakedPixels = (unsigned char *)malloc(pixelCount);
    dl->workPixels  = (unsigned char *)malloc(pixelCount);
    memcpy(dl->bakedPixels, src, pixelCount);
    memcpy(dl->workPixels,  src, pixelCount);
    stbi_image_free(src);

    /* Allocate world position map */
    dl->worldPosMap = (float *)malloc(dl->width * dl->height * 3 * sizeof(float));

    /* Use existing GL texture (the one already loaded by the texture cache) */
    dl->texID = existingTexID;

    /* Build the world position lookup from level mesh, then the coarse grid
       that indexes it (must come second -- it reads worldPosMap). */
    dynLmBuildWorldPosMap(dl, mesh);
    dynLmBuildBlockGrid(dl);

    conLogf("flashlight: dynamic lightmap ready (%dx%d, tex=%u)\n",
           dl->width, dl->height, dl->texID);
    return 1;
}

/* ---- Update lightmap with flashlight contribution ---- */

static void dynLmUpdate(DynLightmap *dl, Vec3 hit,
                        float radius, float intensity, Vec3 color)
{
    if (!dl->bakedPixels || !dl->worldPosMap) return;

    int w = dl->width, h = dl->height;
    float radiusSq = radius * radius;
    float hitX = hit.x, hitY = hit.y, hitZ = hit.z;
    float colorR = color.x, colorG = color.y, colorB = color.z;

    /* Undo the previous frame's light by copying back just the box it wrote.
       Resetting the entire buffer here is a 768KB memcpy at 512x512, which
       costs 1-2ms on a 350MHz PII -- far more than the lit region deserves. */
    if (dl->hasPrev) {
        int rowBytes = (dl->prevMaxX - dl->prevMinX + 1) * 3;
        for (int y = dl->prevMinY; y <= dl->prevMaxY; y++) {
            int off = (y * w + dl->prevMinX) * 3;
            memcpy(dl->workPixels + off, dl->bakedPixels + off, rowBytes);
        }
    }

    /* Find the UV-space box the flashlight can reach, by testing each grid
       cell's world-space bounding sphere against the flashlight sphere. A cell
       can contribute only if the two overlap, so the test is exact -- unlike
       the every-4th-texel walk this replaces, which sampled the whole 3MB
       worldPosMap each frame (~16k near-certain cache misses at 512x512) and
       could miss a UV island thinner than its stride outright.

       Note the box cannot be predicted from the previous frame's: the lightmap
       is an atlas, so texels adjacent in the world may sit anywhere in UV. */
    int scanMinX = w, scanMaxX = 0, scanMinY = h, scanMaxY = 0;

    int cell = 0;
    for (int by = 0; by < dl->blocksY; by++) {
        for (int bx = 0; bx < dl->blocksX; bx++, cell++) {
            if (!dl->blockUsed[cell]) continue;

            const float *sphere = dl->blockSphere + cell * 4;
            float dx = sphere[0] - hitX;
            float dy = sphere[1] - hitY;
            float dz = sphere[2] - hitZ;
            float reach = radius + sphere[3];
            if (dx * dx + dy * dy + dz * dz > reach * reach) continue;

            int x0 = bx * DYNLM_BLOCK, y0 = by * DYNLM_BLOCK;
            int x1 = x0 + DYNLM_BLOCK - 1, y1 = y0 + DYNLM_BLOCK - 1;
            if (x0 < scanMinX) scanMinX = x0;
            if (x1 > scanMaxX) scanMaxX = x1;
            if (y0 < scanMinY) scanMinY = y0;
            if (y1 > scanMaxY) scanMaxY = y1;
        }
    }

    if (scanMinX < 0) scanMinX = 0; if (scanMaxX >= w) scanMaxX = w - 1;
    if (scanMinY < 0) scanMinY = 0; if (scanMaxY >= h) scanMaxY = h - 1;

    /* Fine pass: only scan the affected region */
    for (int y = scanMinY; y <= scanMaxY; y++) {
        for (int x = scanMinX; x <= scanMaxX; x++) {
            int idx3 = (y * w + x) * 3;

            if (dl->worldPosMap[idx3] >= FLT_MAX * 0.5f) continue;

            float dx = dl->worldPosMap[idx3 + 0] - hitX;
            float dy = dl->worldPosMap[idx3 + 1] - hitY;
            float dz = dl->worldPosMap[idx3 + 2] - hitZ;
            float distSq = dx * dx + dy * dy + dz * dz;

            if (distSq < radiusSq) {
                float dist = sqrtf(distSq);
                float t = 1.0f - (dist / radius);
                float atten = t * t * intensity;

                int r = dl->workPixels[idx3 + 0] + (int)(atten * colorR * 255.0f);
                int g = dl->workPixels[idx3 + 1] + (int)(atten * colorG * 255.0f);
                int b = dl->workPixels[idx3 + 2] + (int)(atten * colorB * 255.0f);
                dl->workPixels[idx3 + 0] = (unsigned char)(r > 255 ? 255 : r);
                dl->workPixels[idx3 + 1] = (unsigned char)(g > 255 ? 255 : g);
                dl->workPixels[idx3 + 2] = (unsigned char)(b > 255 ? 255 : b);
            }
        }
    }

    /* Upload the union of the previous and current boxes. Texels outside it
       are untouched baked data the GPU already holds, so a full 768KB upload
       every frame is wasted AGP traffic -- and GL_RGB is the slow path on a
       GeForce 4 MX, which pads each row out to 32-bit internally as it goes.
       Restoring the previous box above is what makes the partial upload safe:
       without it, last frame's light would linger outside the current box. */
    int curValid = (scanMinX <= scanMaxX && scanMinY <= scanMaxY);
    int upMinX = 0, upMinY = 0, upMaxX = 0, upMaxY = 0, haveUp = 0;

    if (curValid) {
        upMinX = scanMinX; upMinY = scanMinY;
        upMaxX = scanMaxX; upMaxY = scanMaxY;
        haveUp = 1;
    }
    if (dl->hasPrev) {
        if (!haveUp) {
            upMinX = dl->prevMinX; upMinY = dl->prevMinY;
            upMaxX = dl->prevMaxX; upMaxY = dl->prevMaxY;
            haveUp = 1;
        } else {
            if (dl->prevMinX < upMinX) upMinX = dl->prevMinX;
            if (dl->prevMinY < upMinY) upMinY = dl->prevMinY;
            if (dl->prevMaxX > upMaxX) upMaxX = dl->prevMaxX;
            if (dl->prevMaxY > upMaxY) upMaxY = dl->prevMaxY;
        }
    }

    if (haveUp) {
        glBindTexture(GL_TEXTURE_2D, dl->texID);
        /* ROW_LENGTH lets GL walk a sub-rect of the full-width buffer.
           ALIGNMENT 1 stops it rounding each RGB row up to a 4-byte
           boundary, which would skew the image for any width where
           width*3 is not a multiple of 4. Both are GL 1.1 core. */
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, w);
        glTexSubImage2D(GL_TEXTURE_2D, 0, upMinX, upMinY,
                        upMaxX - upMinX + 1, upMaxY - upMinY + 1,
                        GL_RGB, GL_UNSIGNED_BYTE,
                        dl->workPixels + (upMinY * w + upMinX) * 3);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }

    if (curValid) {
        dl->prevMinX = scanMinX; dl->prevMinY = scanMinY;
        dl->prevMaxX = scanMaxX; dl->prevMaxY = scanMaxY;
        dl->hasPrev = 1;
    } else {
        dl->hasPrev = 0;
    }

    dl->dirty = 1;
}

/* ---- Restore original lightmap (when flashlight turns off) ---- */

static void dynLmRestore(DynLightmap *dl)
{
    if (!dl->dirty || !dl->bakedPixels) return;

    /* Only the last dirtied box differs from the baked lightmap, so restore
       that and leave the rest of the texture alone. */
    if (dl->hasPrev) {
        int w = dl->width;
        int rowBytes = (dl->prevMaxX - dl->prevMinX + 1) * 3;
        for (int y = dl->prevMinY; y <= dl->prevMaxY; y++) {
            int off = (y * w + dl->prevMinX) * 3;
            memcpy(dl->workPixels + off, dl->bakedPixels + off, rowBytes);
        }

        glBindTexture(GL_TEXTURE_2D, dl->texID);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, w);
        glTexSubImage2D(GL_TEXTURE_2D, 0, dl->prevMinX, dl->prevMinY,
                        dl->prevMaxX - dl->prevMinX + 1,
                        dl->prevMaxY - dl->prevMinY + 1,
                        GL_RGB, GL_UNSIGNED_BYTE,
                        dl->bakedPixels + (dl->prevMinY * w + dl->prevMinX) * 3);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }

    dl->hasPrev = 0;
    dl->dirty = 0;
}

/* ---- Cleanup ---- */

static void dynLmFree(DynLightmap *dl)
{
    free(dl->bakedPixels);
    free(dl->workPixels);
    free(dl->worldPosMap);
    free(dl->blockSphere);
    free(dl->blockUsed);
    /* Don't delete texID — owned by texture cache */
    memset(dl, 0, sizeof(DynLightmap));
}

#endif
