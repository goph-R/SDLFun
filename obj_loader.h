#ifndef OBJ_LOADER_H
#define OBJ_LOADER_H

#include <cstdio>
#include <cstring>
#include <cstdlib>

#define OBJ_MAX_MATERIALS 32
#define OBJ_MAX_SECTORS   32

struct Vec3 {
    float x, y, z;
};

struct Vec2 {
    float u, v;
};

struct Material {
    char name[64];
    char diffusePath[128];    /* map_Kd from MTL */
    char lightmapPath[128];   /* # lm_map from MTL, or derived from name */
    float tilingScale;        /* # tile_scale from MTL, default 1.0 */
    float tilingOffsetX;      /* # tile_offset from MTL, default 0.0 */
    float tilingOffsetY;
};

struct Triangle {
    int v[3];
    int n[3];
    int t[3];  /* texture coordinate indices */
    int materialId;  /* index into materials[], -1 = none */
};

struct Sector {
    int materialId;
    int triStart;   /* first triangle index (after sorting) */
    int triCount;
};

struct ObjMesh {
    Vec3 *verts;
    Vec3 *normals;
    Vec2 *texcoords;
    Triangle *tris;
    int numVerts;
    int numNormals;
    int numTexcoords;
    int numTris;
    int capVerts;
    int capNormals;
    int capTexcoords;
    int capTris;

    Material materials[OBJ_MAX_MATERIALS];
    int numMaterials;
    Sector sectors[OBJ_MAX_SECTORS];
    int numSectors;
};

static void objInit(ObjMesh *m)
{
    m->capVerts = 1024;
    m->capNormals = 1024;
    m->capTexcoords = 1024;
    m->capTris = 2048;
    m->verts = (Vec3 *)malloc(m->capVerts * sizeof(Vec3));
    m->normals = (Vec3 *)malloc(m->capNormals * sizeof(Vec3));
    m->texcoords = (Vec2 *)malloc(m->capTexcoords * sizeof(Vec2));
    m->tris = (Triangle *)malloc(m->capTris * sizeof(Triangle));
    m->numVerts = 0;
    m->numNormals = 0;
    m->numTexcoords = 0;
    m->numTris = 0;
    m->numMaterials = 0;
    m->numSectors = 0;
}

static void objFree(ObjMesh *m)
{
    free(m->verts);
    free(m->normals);
    free(m->texcoords);
    free(m->tris);
}

static void objAddVert(ObjMesh *m, float x, float y, float z)
{
    if (m->numVerts >= m->capVerts) {
        m->capVerts *= 2;
        m->verts = (Vec3 *)realloc(m->verts, m->capVerts * sizeof(Vec3));
    }
    m->verts[m->numVerts].x = x;
    m->verts[m->numVerts].y = y;
    m->verts[m->numVerts].z = z;
    m->numVerts++;
}

static void objAddNormal(ObjMesh *m, float x, float y, float z)
{
    if (m->numNormals >= m->capNormals) {
        m->capNormals *= 2;
        m->normals = (Vec3 *)realloc(m->normals, m->capNormals * sizeof(Vec3));
    }
    m->normals[m->numNormals].x = x;
    m->normals[m->numNormals].y = y;
    m->normals[m->numNormals].z = z;
    m->numNormals++;
}

static void objAddTexcoord(ObjMesh *m, float u, float v)
{
    if (m->numTexcoords >= m->capTexcoords) {
        m->capTexcoords *= 2;
        m->texcoords = (Vec2 *)realloc(m->texcoords, m->capTexcoords * sizeof(Vec2));
    }
    m->texcoords[m->numTexcoords].u = u;
    m->texcoords[m->numTexcoords].v = v;
    m->numTexcoords++;
}

static void objAddTri(ObjMesh *m, int v0, int v1, int v2,
                      int t0, int t1, int t2,
                      int n0, int n1, int n2,
                      int matId)
{
    if (m->numTris >= m->capTris) {
        m->capTris *= 2;
        m->tris = (Triangle *)realloc(m->tris, m->capTris * sizeof(Triangle));
    }
    m->tris[m->numTris].v[0] = v0;
    m->tris[m->numTris].v[1] = v1;
    m->tris[m->numTris].v[2] = v2;
    m->tris[m->numTris].t[0] = t0;
    m->tris[m->numTris].t[1] = t1;
    m->tris[m->numTris].t[2] = t2;
    m->tris[m->numTris].n[0] = n0;
    m->tris[m->numTris].n[1] = n1;
    m->tris[m->numTris].n[2] = n2;
    m->tris[m->numTris].materialId = matId;
    m->numTris++;
}

/* Parse a face index like "1/2/3" or "1//3" or "1" or "1/2" */
static void parseFaceVert(const char *s, int *vi, int *ti, int *ni)
{
    *vi = -1;
    *ti = -1;
    *ni = -1;
    *vi = atoi(s) - 1;
    const char *p = strchr(s, '/');
    if (p) {
        p++;
        if (*p != '/' && *p != '\0') {
            *ti = atoi(p) - 1;
        }
        p = strchr(p, '/');
        if (p) {
            p++;
            *ni = atoi(p) - 1;
        }
    }
}

/* Find material index by name, returns -1 if not found */
static int objFindMaterial(ObjMesh *m, const char *name)
{
    for (int i = 0; i < m->numMaterials; i++) {
        if (strcmp(m->materials[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* Parse a Wavefront MTL file.
   Handles: newmtl, map_Kd, and custom comments # lm_map, # tile_scale */
static void objLoadMtl(ObjMesh *m, const char *mtlPath)
{
    FILE *f = fopen(mtlPath, "r");
    if (!f) {
        fprintf(stderr, "obj: cannot open MTL %s\n", mtlPath);
        return;
    }

    int cur = -1; /* current material index */
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* Strip trailing whitespace */
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' ||
               line[len-1] == ' ' || line[len-1] == '\t'))
            line[--len] = '\0';

        if (strncmp(line, "newmtl ", 7) == 0) {
            if (m->numMaterials >= OBJ_MAX_MATERIALS) continue;
            cur = m->numMaterials++;
            memset(&m->materials[cur], 0, sizeof(Material));
            m->materials[cur].tilingScale = 1.0f;
            m->materials[cur].tilingOffsetX = 0.0f;
            m->materials[cur].tilingOffsetY = 0.0f;
            strncpy(m->materials[cur].name, line + 7, 63);
            m->materials[cur].name[63] = '\0';
            /* Default lightmap name: <materialname>_lm.bmp */
            snprintf(m->materials[cur].lightmapPath, 128, "%s_lm.bmp", line + 7);
        }
        else if (strncmp(line, "map_Kd ", 7) == 0 && cur >= 0) {
            strncpy(m->materials[cur].diffusePath, line + 7, 127);
            m->materials[cur].diffusePath[127] = '\0';
        }
        else if (strncmp(line, "# lm_map ", 9) == 0 && cur >= 0) {
            strncpy(m->materials[cur].lightmapPath, line + 9, 127);
            m->materials[cur].lightmapPath[127] = '\0';
        }
        else if (strncmp(line, "# tile_scale ", 13) == 0 && cur >= 0) {
            m->materials[cur].tilingScale = (float)atof(line + 13);
            if (m->materials[cur].tilingScale <= 0.0f)
                m->materials[cur].tilingScale = 1.0f;
        }
        else if (strncmp(line, "# tile_offset ", 14) == 0 && cur >= 0) {
            sscanf(line + 14, "%f %f",
                   &m->materials[cur].tilingOffsetX,
                   &m->materials[cur].tilingOffsetY);
        }
    }
    fclose(f);
    printf("obj: loaded MTL with %d materials\n", m->numMaterials);
}

static int objLoad(ObjMesh *m, const char *filename)
{
    FILE *f = fopen(filename, "r");
    if (!f) return 0;

    /* Extract directory from filename for resolving mtllib paths */
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

    int currentMat = -1;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'v' && line[1] == ' ') {
            float x, y, z;
            sscanf(line + 2, "%f %f %f", &x, &y, &z);
            objAddVert(m, x, y, z);
        }
        else if (line[0] == 'v' && line[1] == 't' && line[2] == ' ') {
            float u, v;
            sscanf(line + 3, "%f %f", &u, &v);
            objAddTexcoord(m, u, 1.0f - v);
        }
        else if (line[0] == 'v' && line[1] == 'n' && line[2] == ' ') {
            float x, y, z;
            sscanf(line + 3, "%f %f %f", &x, &y, &z);
            objAddNormal(m, x, y, z);
        }
        else if (strncmp(line, "mtllib ", 7) == 0) {
            char mtlName[256];
            sscanf(line + 7, "%255s", mtlName);
            /* Resolve relative to OBJ directory */
            char mtlPath[512];
            snprintf(mtlPath, 512, "%s%s", dir, mtlName);
            objLoadMtl(m, mtlPath);
        }
        else if (strncmp(line, "usemtl ", 7) == 0) {
            char matName[64];
            sscanf(line + 7, "%63s", matName);
            currentMat = objFindMaterial(m, matName);
        }
        else if (line[0] == 'f' && line[1] == ' ') {
            char *tokens[32];
            int count = 0;
            char *tok = strtok(line + 2, " \t\r\n");
            while (tok && count < 32) {
                tokens[count++] = tok;
                tok = strtok(NULL, " \t\r\n");
            }
            if (count >= 3) {
                int vi[32], ti[32], ni[32];
                for (int i = 0; i < count; i++) {
                    parseFaceVert(tokens[i], &vi[i], &ti[i], &ni[i]);
                }
                for (int i = 1; i < count - 1; i++) {
                    objAddTri(m, vi[0], vi[i], vi[i+1],
                                 ti[0], ti[i], ti[i+1],
                                 ni[0], ni[i], ni[i+1],
                                 currentMat);
                }
            }
        }
    }

    fclose(f);
    return 1;
}

/* Sort triangles by materialId and build sector table.
   IMPORTANT: call this AFTER physLoadLevel() since sorting changes triangle order. */
static void objBuildSectors(ObjMesh *m)
{
    if (m->numMaterials == 0) return;

    /* Simple insertion sort by materialId (fine for <5000 tris) */
    for (int i = 1; i < m->numTris; i++) {
        Triangle tmp = m->tris[i];
        int j = i - 1;
        while (j >= 0 && m->tris[j].materialId > tmp.materialId) {
            m->tris[j + 1] = m->tris[j];
            j--;
        }
        m->tris[j + 1] = tmp;
    }

    /* Build sector entries at material boundaries */
    m->numSectors = 0;
    int sectorStart = 0;
    for (int i = 0; i < m->numTris; i++) {
        int isLast = (i == m->numTris - 1);
        int matChanged = (!isLast && m->tris[i + 1].materialId != m->tris[i].materialId);
        if (isLast || matChanged) {
            if (m->numSectors < OBJ_MAX_SECTORS) {
                Sector *s = &m->sectors[m->numSectors];
                s->materialId = m->tris[i].materialId;
                s->triStart = sectorStart;
                s->triCount = i - sectorStart + 1;
                m->numSectors++;
            }
            sectorStart = i + 1;
        }
    }

    printf("obj: built %d sectors\n", m->numSectors);
    for (int i = 0; i < m->numSectors; i++) {
        Sector *s = &m->sectors[i];
        const char *name = (s->materialId >= 0 && s->materialId < m->numMaterials)
                           ? m->materials[s->materialId].name : "(none)";
        printf("  sector %d: material '%s' (%d tris)\n", i, name, s->triCount);
    }
}

#endif
