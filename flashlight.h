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

    /* Indices of the cells the flashlight lit, this frame and last. Work is
       done per cell rather than over their bounding box: in an atlas the lit
       islands can sit in opposite corners, so the box can cover most of the
       texture while the lit area is a few thousand texels. Swapped, not
       copied, at the end of each update. */
    int *curCells, *prevCells;
    int prevCellCount;

    /* One byte per cell, marking those whose texels changed this frame. Used
       to emit the upload as horizontal runs of cells instead of one rectangle
       covering them all. */
    unsigned char *cellDirty;
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
    dl->curCells = (int *)malloc(cells * sizeof(int));
    dl->prevCells = (int *)malloc(cells * sizeof(int));
    dl->cellDirty = (unsigned char *)malloc(cells);
    dl->prevCellCount = 0;

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

    /* Undo the previous frame's light, cell by cell. Restoring its bounding
       box instead would touch every texel between scattered islands. */
    for (int i = 0; i < dl->prevCellCount; i++) {
        int c = dl->prevCells[i];
        int cbx = c % dl->blocksX, cby = c / dl->blocksX;
        int rx0 = cbx * DYNLM_BLOCK, rx1 = rx0 + DYNLM_BLOCK;
        int ry0 = cby * DYNLM_BLOCK, ry1 = ry0 + DYNLM_BLOCK;
        if (rx1 > w) rx1 = w;
        if (ry1 > h) ry1 = h;
        int rowBytes = (rx1 - rx0) * 3;
        for (int y = ry0; y < ry1; y++) {
            int off = (y * w + rx0) * 3;
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

    int cellsHit = 0;
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

            dl->curCells[cellsHit++] = cell;
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

    /* Fine pass: walk only the lit cells. Walking the bounding box instead is
       what kept this expensive -- at 512x512 a box spanning three scattered
       islands is ~160k texels of sqrtf and 3MB-array reads per frame, which is
       ~25ms on a 350MHz PII no matter how little of it is actually lit. */
    for (int i = 0; i < cellsHit; i++) {
        int c = dl->curCells[i];
        int cbx = c % dl->blocksX, cby = c / dl->blocksX;
        int fx0 = cbx * DYNLM_BLOCK, fx1 = fx0 + DYNLM_BLOCK;
        int fy0 = cby * DYNLM_BLOCK, fy1 = fy0 + DYNLM_BLOCK;
        if (fx1 > w) fx1 = w;
        if (fy1 > h) fy1 = h;

        for (int y = fy0; y < fy1; y++) {
            for (int x = fx0; x < fx1; x++) {
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
    }

    int curValid = (scanMinX <= scanMaxX && scanMinY <= scanMaxY);

    /* Upload the changed cells as horizontal runs, not as one rectangle
       covering them all. The lightmap is an atlas, so the cells the flashlight
       touches are scattered: measured on the Win98 box, 92 lit cells (23552
       texels) sat inside a 416x512 box of 212992 texels -- 9x more than was
       actually lit, or 639KB of a 768KB texture pushed every frame. Runs of
       adjacent cells in the same row keep the data proportional to the lit
       area while staying at a couple of dozen glTexSubImage2D calls rather
       than one per cell. */
    int cells = dl->blocksX * dl->blocksY;
    memset(dl->cellDirty, 0, cells);
    for (int i = 0; i < dl->prevCellCount; i++)   /* restored above */
        dl->cellDirty[dl->prevCells[i]] = 1;
    for (int i = 0; i < cellsHit; i++)            /* lit just now */
        dl->cellDirty[dl->curCells[i]] = 1;

    int upCalls = 0, upTexels = 0;

    glBindTexture(GL_TEXTURE_2D, dl->texID);
    /* ROW_LENGTH lets GL walk a sub-rect of the full-width buffer. ALIGNMENT 1
       stops it rounding each RGB row up to a 4-byte boundary, which would skew
       the image for any width where width*3 is not a multiple of 4. Both are
       GL 1.1 core. */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, w);

    for (int by = 0; by < dl->blocksY; by++) {
        int bx = 0;
        while (bx < dl->blocksX) {
            if (!dl->cellDirty[by * dl->blocksX + bx]) { bx++; continue; }

            int runEnd = bx;
            while (runEnd < dl->blocksX &&
                   dl->cellDirty[by * dl->blocksX + runEnd]) runEnd++;

            int ux0 = bx * DYNLM_BLOCK, ux1 = runEnd * DYNLM_BLOCK;
            int uy0 = by * DYNLM_BLOCK, uy1 = uy0 + DYNLM_BLOCK;
            if (ux1 > w) ux1 = w;
            if (uy1 > h) uy1 = h;

            glTexSubImage2D(GL_TEXTURE_2D, 0, ux0, uy0,
                            ux1 - ux0, uy1 - uy0,
                            GL_RGB, GL_UNSIGNED_BYTE,
                            dl->workPixels + (uy0 * w + ux0) * 3);
            upCalls++;
            upTexels += (ux1 - ux0) * (uy1 - uy0);
            bx = runEnd;
        }
    }

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    {   /* This frame's lit cells become next frame's restore list. */
        int *swap = dl->prevCells;
        dl->prevCells = dl->curCells;
        dl->curCells = swap;
        dl->prevCellCount = cellsHit;
    }

    if (curValid) {
        dl->prevMinX = scanMinX; dl->prevMinY = scanMinY;
        dl->prevMaxX = scanMaxX; dl->prevMaxY = scanMaxY;
        dl->hasPrev = 1;
    } else {
        dl->hasPrev = 0;
    }

    dl->dirty = 1;

    /* Diagnostic: how much of the atlas is the box actually covering? Cells
       are 16x16 texels, so cellsHit*256 is the lit area; boxW*boxH is what the
       fine pass above really walked. A large ratio between them means the lit
       islands are scattered across the atlas and the single union box is the
       wrong shape to describe them. */
    {
        static int reportTick = 0;
        if (++reportTick >= 60) {
            reportTick = 0;
            int boxW = curValid ? (scanMaxX - scanMinX + 1) : 0;
            int boxH = curValid ? (scanMaxY - scanMinY + 1) : 0;
            conLogf("flashlight: %d cells lit (%d texels), uploaded %d texels"
                    " in %d calls (box would have been %dx%d = %d)\n",
                    cellsHit, cellsHit * 256, upTexels, upCalls,
                    boxW, boxH, boxW * boxH);
        }
    }
}

/* ---- Restore original lightmap (when flashlight turns off) ---- */

static void dynLmRestore(DynLightmap *dl)
{
    if (!dl->dirty || !dl->bakedPixels) return;

    /* Only the last dirtied box differs from the baked lightmap, so restore
       that and leave the rest of the texture alone. */
    if (dl->hasPrev) {
        int w = dl->width, h = dl->height;
        for (int i = 0; i < dl->prevCellCount; i++) {
            int c = dl->prevCells[i];
            int cbx = c % dl->blocksX, cby = c / dl->blocksX;
            int rx0 = cbx * DYNLM_BLOCK, rx1 = rx0 + DYNLM_BLOCK;
            int ry0 = cby * DYNLM_BLOCK, ry1 = ry0 + DYNLM_BLOCK;
            if (rx1 > w) rx1 = w;
            if (ry1 > h) ry1 = h;
            int rowBytes = (rx1 - rx0) * 3;
            for (int y = ry0; y < ry1; y++) {
                int off = (y * w + rx0) * 3;
                memcpy(dl->workPixels + off, dl->bakedPixels + off, rowBytes);
            }
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
    dl->prevCellCount = 0;
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
    free(dl->curCells);
    free(dl->prevCells);
    free(dl->cellDirty);
    /* Don't delete texID — owned by texture cache */
    memset(dl, 0, sizeof(DynLightmap));
}

#endif
