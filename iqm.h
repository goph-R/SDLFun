#ifndef IQM_H
#define IQM_H

#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <math.h>

#include "texture.h"

/* ---- IQM format constants ---- */

#define IQM_MAGIC "INTERQUAKEMODEL"
#define IQM_VERSION 2
#define IQM_MAX_MESHES 16
#define IQM_MAX_ANIMS  32
#define IQM_MAX_BONES  128

enum {
    IQM_POSITION     = 0,
    IQM_TEXCOORD     = 1,
    IQM_NORMAL       = 2,
    IQM_TANGENT      = 3,
    IQM_BLENDINDEXES = 4,
    IQM_BLENDWEIGHTS = 5
};

enum { IQM_UBYTE = 1, IQM_FLOAT = 7 };
enum { IQM_LOOP = 1 << 0 };

/* ---- On-disk structures (packed, read directly from file) ---- */

#pragma pack(push, 1)
struct IqmHeader {
    char magic[16];
    unsigned int version, filesize, flags;
    unsigned int num_text, ofs_text;
    unsigned int num_meshes, ofs_meshes;
    unsigned int num_vertexarrays, num_vertexes, ofs_vertexarrays;
    unsigned int num_triangles, ofs_triangles, ofs_adjacency;
    unsigned int num_joints, ofs_joints;
    unsigned int num_poses, ofs_poses;
    unsigned int num_anims, ofs_anims;
    unsigned int num_frames, num_framechannels, ofs_frames, ofs_bounds;
    unsigned int num_comment, ofs_comment;
    unsigned int num_extensions, ofs_extensions;
};

struct IqmMeshInfo {
    unsigned int name, material;
    unsigned int first_vertex, num_vertexes;
    unsigned int first_triangle, num_triangles;
};

struct IqmVertexArray {
    unsigned int type, flags, format, size, offset;
};

struct IqmJoint {
    unsigned int name;
    int parent;
    float translate[3], rotate[4], scale[3];
};

struct IqmPose {
    int parent;
    unsigned int mask;
    float channeloffset[10], channelscale[10];
};

struct IqmAnimInfo {
    unsigned int name;
    unsigned int first_frame, num_frames;
    float framerate;
    unsigned int flags;
};

struct IqmTriangle {
    unsigned int vertex[3];
};
#pragma pack(pop)

/* ---- 3x4 matrix math (row-major: [row0 row1 row2], 4 floats each) ---- */

struct Mat34 {
    float m[12]; /* [r0c0 r0c1 r0c2 r0c3 | r1c0 ... | r2c0 ...] */
};

static void mat34Identity(Mat34 *o)
{
    memset(o->m, 0, sizeof(o->m));
    o->m[0] = o->m[5] = o->m[10] = 1.0f;
}

static void mat34FromQuatTransScale(Mat34 *o, const float *q, const float *t, const float *s)
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;

    o->m[0] = (1.0f - (yy + zz)) * s[0];
    o->m[1] = (xy - wz) * s[1];
    o->m[2] = (xz + wy) * s[2];
    o->m[3] = t[0];

    o->m[4] = (xy + wz) * s[0];
    o->m[5] = (1.0f - (xx + zz)) * s[1];
    o->m[6] = (yz - wx) * s[2];
    o->m[7] = t[1];

    o->m[8]  = (xz - wy) * s[0];
    o->m[9]  = (yz + wx) * s[1];
    o->m[10] = (1.0f - (xx + yy)) * s[2];
    o->m[11] = t[2];
}

static void mat34Multiply(Mat34 *o, const Mat34 *a, const Mat34 *b)
{
    const float *A = a->m, *B = b->m;
    float r[12];
    r[0]  = A[0]*B[0] + A[1]*B[4] + A[2]*B[8];
    r[1]  = A[0]*B[1] + A[1]*B[5] + A[2]*B[9];
    r[2]  = A[0]*B[2] + A[1]*B[6] + A[2]*B[10];
    r[3]  = A[0]*B[3] + A[1]*B[7] + A[2]*B[11] + A[3];

    r[4]  = A[4]*B[0] + A[5]*B[4] + A[6]*B[8];
    r[5]  = A[4]*B[1] + A[5]*B[5] + A[6]*B[9];
    r[6]  = A[4]*B[2] + A[5]*B[6] + A[6]*B[10];
    r[7]  = A[4]*B[3] + A[5]*B[7] + A[6]*B[11] + A[7];

    r[8]  = A[8]*B[0] + A[9]*B[4] + A[10]*B[8];
    r[9]  = A[8]*B[1] + A[9]*B[5] + A[10]*B[9];
    r[10] = A[8]*B[2] + A[9]*B[6] + A[10]*B[10];
    r[11] = A[8]*B[3] + A[9]*B[7] + A[10]*B[11] + A[11];
    memcpy(o->m, r, sizeof(r));
}

static void mat34Invert(Mat34 *o, const Mat34 *in)
{
    /* Invert a 3x4 affine matrix (transpose rotation, negate translated-through-transpose) */
    const float *M = in->m;
    o->m[0] = M[0]; o->m[1] = M[4]; o->m[2]  = M[8];
    o->m[4] = M[1]; o->m[5] = M[5]; o->m[6]  = M[9];
    o->m[8] = M[2]; o->m[9] = M[6]; o->m[10] = M[10];
    o->m[3]  = -(M[0]*M[3] + M[4]*M[7] + M[8]*M[11]);
    o->m[7]  = -(M[1]*M[3] + M[5]*M[7] + M[9]*M[11]);
    o->m[11] = -(M[2]*M[3] + M[6]*M[7] + M[10]*M[11]);
}

static void mat34TransformPos(float *out, const Mat34 *m, const float *v)
{
    const float *M = m->m;
    out[0] = M[0]*v[0] + M[1]*v[1] + M[2]*v[2]  + M[3];
    out[1] = M[4]*v[0] + M[5]*v[1] + M[6]*v[2]  + M[7];
    out[2] = M[8]*v[0] + M[9]*v[1] + M[10]*v[2] + M[11];
}

static void mat34TransformNorm(float *out, const Mat34 *m, const float *v)
{
    const float *M = m->m;
    out[0] = M[0]*v[0] + M[1]*v[1] + M[2]*v[2];
    out[1] = M[4]*v[0] + M[5]*v[1] + M[6]*v[2];
    out[2] = M[8]*v[0] + M[9]*v[1] + M[10]*v[2];
}

static float quatNormalize(float *q)
{
    float len = sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    if (len > 0.0f) { float inv = 1.0f / len; q[0]*=inv; q[1]*=inv; q[2]*=inv; q[3]*=inv; }
    return len;
}

/* ---- Runtime IQM model ---- */

struct IqmMesh {
    char name[64];
    char materialName[64];
    unsigned int firstVert, numVerts;
    unsigned int firstTri, numTris;
    GLuint texID;
};

struct IqmAnim {
    char name[64];
    unsigned int firstFrame, numFrames;
    float framerate;
    int loop;
};

struct IqmModel {
    /* Vertex data (pointers into fileData buffer) */
    float *inPosition;      /* numVerts * 3 */
    float *inNormal;        /* numVerts * 3 */
    float *inTexcoord;      /* numVerts * 2 */
    unsigned char *inBlendIndex;  /* numVerts * 4 */
    unsigned char *inBlendWeight; /* numVerts * 4 */
    unsigned int numVerts;

    /* Triangles (pointer into fileData) */
    IqmTriangle *triangles;
    unsigned int numTris;

    /* Meshes */
    IqmMesh meshes[IQM_MAX_MESHES];
    int numMeshes;

    /* Skeleton */
    IqmJoint *joints;       /* pointer into fileData */
    int numBones;
    Mat34 *baseframe;       /* bind pose per bone */
    Mat34 *invBaseframe;    /* inverse bind pose per bone */

    /* Animation */
    IqmAnim anims[IQM_MAX_ANIMS];
    int numAnims;
    Mat34 *frames;          /* pre-computed: numFrames * numBones matrices */
    int numFrames;

    /* Skinning output (rewritten every frame) */
    float *outPosition;     /* numVerts * 3 */
    float *outNormal;       /* numVerts * 3 */
    Mat34 *outBones;        /* numBones final bone matrices */

    /* Raw file data (owns memory) */
    unsigned char *fileData;
};

static void iqmFree(IqmModel *mdl)
{
    if (!mdl) return;
    free(mdl->baseframe);
    free(mdl->invBaseframe);
    free(mdl->frames);
    free(mdl->outPosition);
    free(mdl->outNormal);
    free(mdl->outBones);
    free(mdl->fileData);
    for (int i = 0; i < mdl->numMeshes; i++) {
        if (mdl->meshes[i].texID)
            glDeleteTextures(1, &mdl->meshes[i].texID);
    }
    memset(mdl, 0, sizeof(IqmModel));
}

static int iqmLoad(IqmModel *mdl, const char *filename)
{
    memset(mdl, 0, sizeof(IqmModel));

    FILE *f = fopen(filename, "rb");
    if (!f) { fprintf(stderr, "iqm: cannot open %s\n", filename); return 0; }

    /* Read header */
    IqmHeader hdr;
    if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
        memcmp(hdr.magic, IQM_MAGIC, 16) != 0 || hdr.version != IQM_VERSION) {
        fprintf(stderr, "iqm: invalid header in %s\n", filename);
        fclose(f);
        return 0;
    }

    if (hdr.filesize > 16 * 1024 * 1024) {
        fprintf(stderr, "iqm: file too large %s\n", filename);
        fclose(f);
        return 0;
    }

    /* Read entire file */
    mdl->fileData = (unsigned char *)malloc(hdr.filesize);
    memcpy(mdl->fileData, &hdr, sizeof(hdr));
    if (fread(mdl->fileData + sizeof(hdr), 1, hdr.filesize - sizeof(hdr), f) !=
        hdr.filesize - sizeof(hdr)) {
        fprintf(stderr, "iqm: read error %s\n", filename);
        fclose(f);
        iqmFree(mdl);
        return 0;
    }
    fclose(f);

    unsigned char *buf = mdl->fileData;
    const char *str = hdr.ofs_text ? (char *)&buf[hdr.ofs_text] : "";

    /* ---- Parse vertex arrays ---- */
    mdl->numVerts = hdr.num_vertexes;
    IqmVertexArray *vas = (IqmVertexArray *)&buf[hdr.ofs_vertexarrays];
    for (unsigned int i = 0; i < hdr.num_vertexarrays; i++) {
        IqmVertexArray *va = &vas[i];
        switch (va->type) {
        case IQM_POSITION:     if (va->format == IQM_FLOAT && va->size == 3) mdl->inPosition = (float *)&buf[va->offset]; break;
        case IQM_NORMAL:       if (va->format == IQM_FLOAT && va->size == 3) mdl->inNormal = (float *)&buf[va->offset]; break;
        case IQM_TEXCOORD:     if (va->format == IQM_FLOAT && va->size == 2) mdl->inTexcoord = (float *)&buf[va->offset]; break;
        case IQM_BLENDINDEXES: if (va->format == IQM_UBYTE && va->size == 4) mdl->inBlendIndex = (unsigned char *)&buf[va->offset]; break;
        case IQM_BLENDWEIGHTS: if (va->format == IQM_UBYTE && va->size == 4) mdl->inBlendWeight = (unsigned char *)&buf[va->offset]; break;
        }
    }

    if (!mdl->inPosition) {
        fprintf(stderr, "iqm: no position data in %s\n", filename);
        iqmFree(mdl);
        return 0;
    }

    /* ---- Parse triangles ---- */
    mdl->numTris = hdr.num_triangles;
    mdl->triangles = (IqmTriangle *)&buf[hdr.ofs_triangles];

    /* ---- Parse meshes ---- */
    mdl->numMeshes = (int)hdr.num_meshes;
    if (mdl->numMeshes > IQM_MAX_MESHES) mdl->numMeshes = IQM_MAX_MESHES;
    IqmMeshInfo *meshInfos = (IqmMeshInfo *)&buf[hdr.ofs_meshes];
    for (int i = 0; i < mdl->numMeshes; i++) {
        IqmMeshInfo *mi = &meshInfos[i];
        strncpy(mdl->meshes[i].name, &str[mi->name], 63);
        strncpy(mdl->meshes[i].materialName, &str[mi->material], 63);
        mdl->meshes[i].firstVert = mi->first_vertex;
        mdl->meshes[i].numVerts = mi->num_vertexes;
        mdl->meshes[i].firstTri = mi->first_triangle;
        mdl->meshes[i].numTris = mi->num_triangles;
        mdl->meshes[i].texID = 0;
        printf("iqm: mesh %d: '%s' material='%s' (%d verts, %d tris)\n",
               i, mdl->meshes[i].name, mdl->meshes[i].materialName,
               mi->num_vertexes, mi->num_triangles);
    }

    /* ---- Parse joints (bind pose skeleton) ---- */
    mdl->numBones = (int)hdr.num_joints;
    mdl->joints = (IqmJoint *)&buf[hdr.ofs_joints];
    mdl->baseframe = (Mat34 *)malloc(mdl->numBones * sizeof(Mat34));
    mdl->invBaseframe = (Mat34 *)malloc(mdl->numBones * sizeof(Mat34));

    for (int i = 0; i < mdl->numBones; i++) {
        IqmJoint *j = &mdl->joints[i];
        float q[4] = { j->rotate[0], j->rotate[1], j->rotate[2], j->rotate[3] };
        quatNormalize(q);
        mat34FromQuatTransScale(&mdl->baseframe[i], q, j->translate, j->scale);
        mat34Invert(&mdl->invBaseframe[i], &mdl->baseframe[i]);
        if (j->parent >= 0) {
            Mat34 tmp;
            mat34Multiply(&tmp, &mdl->baseframe[j->parent], &mdl->baseframe[i]);
            mdl->baseframe[i] = tmp;
            mat34Multiply(&tmp, &mdl->invBaseframe[i], &mdl->invBaseframe[j->parent]);
            mdl->invBaseframe[i] = tmp;
        }
    }

    printf("iqm: %d bones\n", mdl->numBones);

    /* ---- Parse animations and pre-compute frame matrices ---- */
    mdl->numFrames = (int)hdr.num_frames;
    mdl->numAnims = (int)hdr.num_anims;
    if (mdl->numAnims > IQM_MAX_ANIMS) mdl->numAnims = IQM_MAX_ANIMS;

    if (hdr.num_anims > 0 && hdr.num_poses == hdr.num_joints) {
        IqmAnimInfo *animInfos = (IqmAnimInfo *)&buf[hdr.ofs_anims];
        for (int i = 0; i < mdl->numAnims; i++) {
            strncpy(mdl->anims[i].name, &str[animInfos[i].name], 63);
            mdl->anims[i].firstFrame = animInfos[i].first_frame;
            mdl->anims[i].numFrames = animInfos[i].num_frames;
            mdl->anims[i].framerate = animInfos[i].framerate;
            mdl->anims[i].loop = (animInfos[i].flags & IQM_LOOP) ? 1 : 0;
            printf("iqm: anim %d: '%s' (%d frames, %.1f fps, %s)\n",
                   i, mdl->anims[i].name, mdl->anims[i].numFrames,
                   mdl->anims[i].framerate, mdl->anims[i].loop ? "loop" : "once");
        }

        IqmPose *poses = (IqmPose *)&buf[hdr.ofs_poses];
        unsigned short *framedata = (unsigned short *)&buf[hdr.ofs_frames];

        mdl->frames = (Mat34 *)malloc(mdl->numFrames * mdl->numBones * sizeof(Mat34));

        for (int i = 0; i < mdl->numFrames; i++) {
            for (int j = 0; j < mdl->numBones; j++) {
                IqmPose *p = &poses[j];
                float translate[3], rotate[4], scale[3];

                translate[0] = p->channeloffset[0]; if (p->mask & 0x001) translate[0] += *framedata++ * p->channelscale[0];
                translate[1] = p->channeloffset[1]; if (p->mask & 0x002) translate[1] += *framedata++ * p->channelscale[1];
                translate[2] = p->channeloffset[2]; if (p->mask & 0x004) translate[2] += *framedata++ * p->channelscale[2];
                rotate[0]    = p->channeloffset[3]; if (p->mask & 0x008) rotate[0]    += *framedata++ * p->channelscale[3];
                rotate[1]    = p->channeloffset[4]; if (p->mask & 0x010) rotate[1]    += *framedata++ * p->channelscale[4];
                rotate[2]    = p->channeloffset[5]; if (p->mask & 0x020) rotate[2]    += *framedata++ * p->channelscale[5];
                rotate[3]    = p->channeloffset[6]; if (p->mask & 0x040) rotate[3]    += *framedata++ * p->channelscale[6];
                scale[0]     = p->channeloffset[7]; if (p->mask & 0x080) scale[0]     += *framedata++ * p->channelscale[7];
                scale[1]     = p->channeloffset[8]; if (p->mask & 0x100) scale[1]     += *framedata++ * p->channelscale[8];
                scale[2]     = p->channeloffset[9]; if (p->mask & 0x200) scale[2]     += *framedata++ * p->channelscale[9];

                quatNormalize(rotate);
                Mat34 local;
                mat34FromQuatTransScale(&local, rotate, translate, scale);

                /* Pre-concatenate: parentBasePose * localPose * inverseBindPose */
                Mat34 *out = &mdl->frames[i * mdl->numBones + j];
                if (p->parent >= 0) {
                    Mat34 tmp;
                    mat34Multiply(&tmp, &local, &mdl->invBaseframe[j]);
                    mat34Multiply(out, &mdl->baseframe[p->parent], &tmp);
                } else {
                    mat34Multiply(out, &local, &mdl->invBaseframe[j]);
                }
            }
        }
    }

    /* ---- Allocate skinning output buffers ---- */
    mdl->outPosition = (float *)malloc(mdl->numVerts * 3 * sizeof(float));
    mdl->outNormal   = (float *)malloc(mdl->numVerts * 3 * sizeof(float));
    mdl->outBones    = (Mat34 *)malloc(mdl->numBones * sizeof(Mat34));

    printf("iqm: loaded %s (%d verts, %d tris, %d bones, %d anims, %d frames)\n",
           filename, mdl->numVerts, mdl->numTris, mdl->numBones,
           mdl->numAnims, mdl->numFrames);
    return 1;
}

/* Load textures for each mesh sub-part. Call after GL context is ready.
   Looks for texture files relative to the model directory. */
static void iqmLoadTextures(IqmModel *mdl, const char *modelPath, TexCache *cache)
{
    /* Extract directory from model path */
    char dir[256];
    dir[0] = '\0';
    const char *lastSlash = strrchr(modelPath, '/');
    if (!lastSlash) lastSlash = strrchr(modelPath, '\\');
    if (lastSlash) {
        int dirLen = (int)(lastSlash - modelPath + 1);
        if (dirLen > 255) dirLen = 255;
        memcpy(dir, modelPath, dirLen);
        dir[dirLen] = '\0';
    }

    for (int i = 0; i < mdl->numMeshes; i++) {
        char texPath[256];
        snprintf(texPath, 256, "%s%s", dir, mdl->meshes[i].materialName);
        mdl->meshes[i].texID = texCacheGet(cache, texPath, GL_CLAMP_TO_EDGE);
    }
}

/* ---- CPU skinning: evaluate animation and transform vertices ---- */

static void iqmAnimate(IqmModel *mdl, float curframe)
{
    if (!mdl->frames || mdl->numFrames == 0) {
        /* No animation — copy bind pose positions */
        if (mdl->inPosition)
            memcpy(mdl->outPosition, mdl->inPosition, mdl->numVerts * 3 * sizeof(float));
        if (mdl->inNormal)
            memcpy(mdl->outNormal, mdl->inNormal, mdl->numVerts * 3 * sizeof(float));
        return;
    }

    /* Interpolate between two frames */
    int frame1 = (int)curframe;
    int frame2 = frame1 + 1;
    float lerp = curframe - (float)frame1;
    frame1 %= mdl->numFrames;
    frame2 %= mdl->numFrames;

    Mat34 *mat1 = &mdl->frames[frame1 * mdl->numBones];
    Mat34 *mat2 = &mdl->frames[frame2 * mdl->numBones];

    /* Interpolate bone matrices and walk hierarchy */
    for (int i = 0; i < mdl->numBones; i++) {
        /* Lerp each element of the 3x4 matrix */
        Mat34 blended;
        for (int k = 0; k < 12; k++)
            blended.m[k] = mat1[i].m[k] * (1.0f - lerp) + mat2[i].m[k] * lerp;

        if (mdl->joints[i].parent >= 0)
            mat34Multiply(&mdl->outBones[i], &mdl->outBones[mdl->joints[i].parent], &blended);
        else
            mdl->outBones[i] = blended;
    }

    /* Skin vertices */
    for (unsigned int i = 0; i < mdl->numVerts; i++) {
        const float *srcPos  = &mdl->inPosition[i * 3];
        const float *srcNorm = mdl->inNormal ? &mdl->inNormal[i * 3] : NULL;
        const unsigned char *idx = &mdl->inBlendIndex[i * 4];
        const unsigned char *wgt = &mdl->inBlendWeight[i * 4];
        float *dstPos  = &mdl->outPosition[i * 3];
        float *dstNorm = &mdl->outNormal[i * 3];

        /* Blend bone matrices */
        Mat34 skinMat;
        memset(&skinMat, 0, sizeof(skinMat));
        for (int j = 0; j < 4 && wgt[j]; j++) {
            float w = wgt[j] / 255.0f;
            const Mat34 *bone = &mdl->outBones[idx[j]];
            for (int k = 0; k < 12; k++)
                skinMat.m[k] += bone->m[k] * w;
        }

        mat34TransformPos(dstPos, &skinMat, srcPos);
        if (srcNorm) {
            mat34TransformNorm(dstNorm, &skinMat, srcNorm);
            /* Renormalize */
            float len = sqrtf(dstNorm[0]*dstNorm[0] + dstNorm[1]*dstNorm[1] + dstNorm[2]*dstNorm[2]);
            if (len > 0.0001f) { dstNorm[0] /= len; dstNorm[1] /= len; dstNorm[2] /= len; }
        }
    }
}

/* ---- Render the skinned model with OpenGL 1.x ---- */

static void iqmRender(IqmModel *mdl)
{
    float *pos  = (mdl->numFrames > 0) ? mdl->outPosition : mdl->inPosition;
    float *norm = (mdl->numFrames > 0) ? mdl->outNormal   : mdl->inNormal;

    for (int m = 0; m < mdl->numMeshes; m++) {
        IqmMesh *mesh = &mdl->meshes[m];

        if (mesh->texID) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, mesh->texID);
        }

        glBegin(GL_TRIANGLES);
        for (unsigned int t = 0; t < mesh->numTris; t++) {
            IqmTriangle *tri = &mdl->triangles[mesh->firstTri + t];
            for (int j = 0; j < 3; j++) {
                unsigned int vi = tri->vertex[j];
                if (norm) glNormal3fv(&norm[vi * 3]);
                if (mdl->inTexcoord) glTexCoord2fv(&mdl->inTexcoord[vi * 2]);
                glVertex3fv(&pos[vi * 3]);
            }
        }
        glEnd();

        if (mesh->texID) glDisable(GL_TEXTURE_2D);
    }
}

/* ---- Find animation index by name ---- */

static int iqmFindAnim(IqmModel *mdl, const char *name)
{
    for (int i = 0; i < mdl->numAnims; i++) {
        if (strcmp(mdl->anims[i].name, name) == 0) return i;
    }
    return -1;
}

#endif
