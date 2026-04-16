#ifndef OBJ_LOADER_H
#define OBJ_LOADER_H

#include <cstdio>
#include <cstring>
#include <cstdlib>

struct Vec3 {
    float x, y, z;
};

struct Vec2 {
    float u, v;
};

struct Triangle {
    int v[3];
    int n[3];
    int t[3];  /* texture coordinate indices */
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
                      int n0, int n1, int n2)
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

static int objLoad(ObjMesh *m, const char *filename)
{
    FILE *f = fopen(filename, "r");
    if (!f) return 0;

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
                                 ni[0], ni[i], ni[i+1]);
                }
            }
        }
    }

    fclose(f);
    return 1;
}

#endif
