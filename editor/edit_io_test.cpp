/*
 * edit_io_test.cpp — Linux headless check for M6 save/load/export.
 *   g++ -I. -I../SOOB-Core editor/edit_io_test.cpp -o /tmp/eiotest && /tmp/eiotest
 */
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>

static void conLogf(const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }

#include "edit_mesh.h"
#include "edit_io.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { \
    printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
#define NEAR(a, b) (fabsf((float)(a) - (float)(b)) < 1e-3f)

static int fileHas(const char *path, const char *needle)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char buf[8192]; size_t n = fread(buf, 1, sizeof(buf) - 1, f); buf[n] = 0;
    fclose(f);
    return strstr(buf, needle) != NULL;
}

int main(void)
{
    const char *lvl = "/tmp/claude-1000/io_cube.lvl";
    const char *obj = "/tmp/claude-1000/io_cube.obj";
    const char *mtl = "/tmp/claude-1000/io_cube.mtl";

    EditMesh m; editMeshInit(&m);
    memset(&m.mats[0], 0, sizeof(Material));
    strcpy(m.mats[0].name, "wall");
    strcpy(m.mats[0].diffusePath, "assets/textures/wood1.png");
    m.mats[0].tilingScale = 0.5f;
    m.mats[0].tilingOffsetX = 0.25f;
    m.mats[0].tilingOffsetY = 0.0f;
    m.numMats = 1;
    editAddCube(&m, 0, 1, 0, 2, 2, 2, 0);

    CHECK(editSaveLvl(&m, lvl) == 1);

    /* round-trip: load into a fresh mesh, compare */
    EditMesh r; editLoadLvl(&r, lvl);
    CHECK(r.numVerts == 8);
    CHECK(r.numFaces == 6);
    CHECK(r.numMats == 1);
    CHECK(strcmp(r.mats[0].name, "wall") == 0);
    CHECK(strcmp(r.mats[0].diffusePath, "assets/textures/wood1.png") == 0);
    CHECK(NEAR(r.mats[0].tilingScale, 0.5f));
    CHECK(NEAR(r.mats[0].tilingOffsetX, 0.25f));
    CHECK(r.faces[0].nv == 4);
    CHECK(NEAR(r.verts[2].pos.y, m.verts[2].pos.y));   /* geometry preserved */
    CHECK(r.faces[3].materialId == 0);

    /* OBJ + MTL export */
    CHECK(editExportObj(&m, obj, mtl) == 1);
    CHECK(fileHas(obj, "mtllib io_cube.mtl"));
    CHECK(fileHas(obj, "usemtl wall"));
    CHECK(fileHas(obj, "vn "));            /* per-face normals present */
    CHECK(fileHas(obj, "//"));             /* v//vn faces */
    CHECK(fileHas(mtl, "newmtl wall"));
    CHECK(fileHas(mtl, "map_Kd assets/textures/wood1.png"));
    CHECK(fileHas(mtl, "# tile_scale 0.5"));
    CHECK(fileHas(mtl, "# tile_offset 0.25"));

    editMeshFree(&m); editMeshFree(&r);
    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
