#ifndef EDIT_IO_H
#define EDIT_IO_H

/* ---- Editor save / load / export (M6) -------------------------------------
 *
 * Native `.lvl` (the editor's editable document) and Wavefront OBJ+MTL export
 * (for the engine, and for a Blender lightmap bake). Plain line-based text via
 * stdio — pure, testable on Linux, no GL/FLTK.
 *
 * .lvl format (v1):
 *     # SOOB level v1
 *     snap 0.01
 *     mat <name> <diffusePath|-> <scale> <offX> <offY>
 *     v <x> <y> <z>
 *     f <nv> <i> <j> <k> [<l>] <matId>
 *   Tokens are whitespace-separated (no spaces in names/paths); '-' marks empty.
 *
 * OBJ export writes one FLAT normal per face (v//vn faces) so the engine's
 * box-mapping picks the right projection axis — without normals every face
 * would project as a floor. No UVs: diffuse tiles from world space, and Blender
 * re-unwraps for the lightmap bake. MTL carries the engine's custom
 * `# tile_scale` / `# tile_offset` comments so tiling survives the round-trip.
 *
 * Include after edit_mesh.h; needs a forward-declared conLogf.
 * -------------------------------------------------------------------------- */

#include <cstdio>
#include <cstring>
#include "edit_mesh.h"

static int editSaveLvl(EditMesh *m, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) { conLogf("save: cannot open %s\n", path); return 0; }
    fprintf(f, "# SOOB level v1\n");
    fprintf(f, "snap %.2f\n", EDIT_SNAP);
    int i, j;
    for (i = 0; i < m->numMats; i++) {
        Material *mt = &m->mats[i];
        fprintf(f, "mat %s %s %g %g %g\n",
                mt->name[0] ? mt->name : "-",
                mt->diffusePath[0] ? mt->diffusePath : "-",
                mt->tilingScale, mt->tilingOffsetX, mt->tilingOffsetY);
    }
    for (i = 0; i < m->numVerts; i++)
        fprintf(f, "v %.4f %.4f %.4f\n",
                m->verts[i].pos.x, m->verts[i].pos.y, m->verts[i].pos.z);
    for (i = 0; i < m->numFaces; i++) {
        EditFace *fc = &m->faces[i];
        fprintf(f, "f %d", fc->nv);
        for (j = 0; j < fc->nv; j++) fprintf(f, " %d", fc->v[j]);
        fprintf(f, " %d\n", fc->materialId);
    }
    fclose(f);
    conLogf("save: %d verts, %d faces, %d mats -> %s\n",
            m->numVerts, m->numFaces, m->numMats, path);
    return 1;
}

/* Replace *m with the mesh from `path`. Leaves an empty (valid) mesh on error. */
static int editLoadLvl(EditMesh *m, const char *path)
{
    editMeshInit(m);
    FILE *f = fopen(path, "r");
    if (!f) { conLogf("open: cannot open %s\n", path); return 0; }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char tok[16];
        if (sscanf(line, "%15s", tok) != 1) continue;
        if (tok[0] == '#') continue;

        if (tok[0] == 'v' && tok[1] == 0) {
            float x, y, z;
            if (sscanf(line, "v %f %f %f", &x, &y, &z) == 3) editAddVert(m, x, y, z);
        } else if (tok[0] == 'f' && tok[1] == 0) {
            int nv, v[4], mat;
            if (sscanf(line, "f %d", &nv) == 1 && nv == 3) {
                if (sscanf(line, "f %d %d %d %d %d", &nv, &v[0], &v[1], &v[2], &mat) == 5)
                    editAddFace(m, v[0], v[1], v[2], -1, mat);
            } else if (nv == 4) {
                if (sscanf(line, "f %d %d %d %d %d %d",
                           &nv, &v[0], &v[1], &v[2], &v[3], &mat) == 6)
                    editAddFace(m, v[0], v[1], v[2], v[3], mat);
            }
        } else if (strcmp(tok, "mat") == 0) {
            char nm[64], dp[128];
            float s, ox, oy;
            if (sscanf(line, "mat %63s %127s %f %f %f", nm, dp, &s, &ox, &oy) == 5
                && m->numMats < OBJ_MAX_MATERIALS) {
                Material *mt = &m->mats[m->numMats++];
                memset(mt, 0, sizeof(Material));
                if (strcmp(nm, "-") != 0) strncpy(mt->name, nm, 63);
                if (strcmp(dp, "-") != 0) strncpy(mt->diffusePath, dp, 127);
                mt->tilingScale = s; mt->tilingOffsetX = ox; mt->tilingOffsetY = oy;
                mt->alphaRef = 0.5f;
            }
        }
        /* `snap` line is informational — we always snap to EDIT_SNAP. */
    }
    fclose(f);
    conLogf("open: %d verts, %d faces, %d mats from %s\n",
            m->numVerts, m->numFaces, m->numMats, path);
    return 1;
}

static const char *editBasename(const char *p)
{
    const char *a = strrchr(p, '/');
    const char *b = strrchr(p, '\\');
    const char *s = (a > b) ? a : b;
    return s ? s + 1 : p;
}

/* Export OBJ (+ its MTL). One flat normal per face; faces grouped by material. */
static int editExportObj(EditMesh *m, const char *objPath, const char *mtlPath)
{
    FILE *f = fopen(objPath, "w");
    if (!f) { conLogf("export: cannot open %s\n", objPath); return 0; }
    fprintf(f, "# SOOB level export\n");
    fprintf(f, "mtllib %s\n", editBasename(mtlPath));

    int i, j, mm;
    for (i = 0; i < m->numVerts; i++)
        fprintf(f, "v %.4f %.4f %.4f\n",
                m->verts[i].pos.x, m->verts[i].pos.y, m->verts[i].pos.z);

    int ngrp = m->numMats > 0 ? m->numMats : 1;

    /* normals: one per face, in the same material-grouped order the faces use */
    for (mm = 0; mm < ngrp; mm++)
        for (i = 0; i < m->numFaces; i++) {
            int mid = m->faces[i].materialId;
            if (mid < 0 || mid >= m->numMats) mid = 0;
            if (mid != mm) continue;
            editFaceComputeNormal(m, &m->faces[i]);
            fprintf(f, "vn %.6f %.6f %.6f\n",
                    m->faces[i].normal.x, m->faces[i].normal.y, m->faces[i].normal.z);
        }

    int vn = 0;
    for (mm = 0; mm < ngrp; mm++) {
        if (mm < m->numMats)
            fprintf(f, "usemtl %s\n", m->mats[mm].name[0] ? m->mats[mm].name : "material");
        for (i = 0; i < m->numFaces; i++) {
            int mid = m->faces[i].materialId;
            if (mid < 0 || mid >= m->numMats) mid = 0;
            if (mid != mm) continue;
            vn++;                                  /* matches the vn order above */
            EditFace *fc = &m->faces[i];
            fprintf(f, "f");
            for (j = 0; j < fc->nv; j++) fprintf(f, " %d//%d", fc->v[j] + 1, vn);
            fprintf(f, "\n");
        }
    }
    fclose(f);

    FILE *mf = fopen(mtlPath, "w");
    if (!mf) { conLogf("export: cannot open %s\n", mtlPath); return 0; }
    for (i = 0; i < m->numMats; i++) {
        Material *mt = &m->mats[i];
        fprintf(mf, "newmtl %s\n", mt->name[0] ? mt->name : "material");
        if (mt->diffusePath[0]) fprintf(mf, "map_Kd %s\n", mt->diffusePath);
        fprintf(mf, "# tile_scale %g\n", mt->tilingScale);
        fprintf(mf, "# tile_offset %g %g\n\n", mt->tilingOffsetX, mt->tilingOffsetY);
    }
    fclose(mf);
    conLogf("export: %s + %s\n", objPath, mtlPath);
    return 1;
}

#endif /* EDIT_IO_H */
