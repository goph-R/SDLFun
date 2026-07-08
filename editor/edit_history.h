#ifndef EDIT_HISTORY_H
#define EDIT_HISTORY_H

/* ---- Unified editor undo/redo (EM1) ---------------------------------------
 *
 * ONE tagged snapshot stack spanning BOTH editor documents — the geometry mesh
 * (EditMesh) and the entity list (EntityList) — so a single Ctrl+Z restores
 * whichever document changed last and mesh/entity edits interleave correctly.
 *
 *   - Mesh entries reuse editMeshCopy/editMeshFree (edit_undo.h) verbatim.
 *   - Entity entries are COMPACT: only the editable per-entity fields (active,
 *     type, name, group, transform, scale, and the type-specific union), NOT the
 *     ~16 KB inline ObjMesh/IqmModel/physBody each Entity carries. So a 64-deep
 *     stack of entity snapshots stays a few MB, not hundreds, and restoring a
 *     move/rotate/field-edit leaves the entity's loaded mesh untouched.
 *
 * This is the editor's undo from EM1 on; it supersedes bare EditHistory in
 * editor.cpp. It is NOT included by the pure-mesh headless tests, so entity.h
 * (and its GL/asset deps) never leak into them. Include after edit_undo.h and
 * entity.h.
 *
 * The compact entity snapshot is intentionally minimal (covers move/rotate/
 * add/delete + name/group/scale/union edits). If a later milestone edits the
 * mesh/anim *identity* fields, extend EntSnap + the two copy loops together.
 * -------------------------------------------------------------------------- */

#include <cstddef>          /* offsetof */
#include <cstdlib>
#include <cstring>
#include "edit_undo.h"      /* EditMesh + editMeshCopy/editMeshFree */
#include "entity.h"         /* Entity, EntityList, MAX_ENTITIES */

/* The type-specific union is Entity's final member, so the byte range
   [offsetof(item), sizeof(Entity)) is exactly that union (+ any trailing pad).
   Snapshot it verbatim rather than per-type, so every union variant round-trips
   without a type switch. */
typedef struct {
    int   active, type;
    char  name[32], group[32];
    float posX, posY, posZ, rotY, scale;
    unsigned char u[sizeof(Entity) - offsetof(Entity, item)];
} EntSnap;

typedef struct { EntSnap *e; int count; } EntListSnap;   /* captures [0..count) */

static void entListSnapCapture(EntListSnap *s, const EntityList *l)
{
    int i, n = l->count;
    s->count = n;
    s->e = (EntSnap *)malloc((n > 0 ? n : 1) * sizeof(EntSnap));
    for (i = 0; i < n; i++) {
        const Entity *e = &l->entities[i];
        EntSnap *d = &s->e[i];
        d->active = e->active; d->type = (int)e->type;
        memcpy(d->name, e->name, 32); memcpy(d->group, e->group, 32);
        d->posX = e->posX; d->posY = e->posY; d->posZ = e->posZ;
        d->rotY = e->rotY; d->scale = e->scale;
        memcpy(d->u, (const unsigned char *)e + offsetof(Entity, item), sizeof(d->u));
    }
}

static void entListSnapRestore(EntityList *l, const EntListSnap *s)
{
    int i;
    l->count = s->count;
    l->playerIndex = -1;
    for (i = 0; i < s->count; i++) {
        const EntSnap *d = &s->e[i];
        Entity *e = &l->entities[i];
        e->active = d->active; e->type = (EntityType)d->type;
        memcpy(e->name, d->name, 32); memcpy(e->group, d->group, 32);
        e->posX = d->posX; e->posY = d->posY; e->posZ = d->posZ;
        e->rotY = d->rotY; e->scale = d->scale;
        memcpy((unsigned char *)e + offsetof(Entity, item), d->u, sizeof(d->u));
        if (e->active && e->type == ENT_PLAYER) l->playerIndex = i;
    }
}

static void entListSnapFree(EntListSnap *s) { free(s->e); s->e = NULL; s->count = 0; }

/* --- tagged stack ----------------------------------------------------------- */

typedef enum { DOC_MESH = 0, DOC_ENTS = 1 } DocSnapKind;

typedef struct {
    DocSnapKind kind;
    EditMesh    mesh;     /* valid when kind == DOC_MESH */
    EntListSnap ents;     /* valid when kind == DOC_ENTS */
} DocSnap;

typedef struct {
    DocSnap undo[EDIT_UNDO_MAX]; int nUndo;
    DocSnap redo[EDIT_UNDO_MAX]; int nRedo;
} DocHistory;

static void docSnapFree(DocSnap *s)
{
    if (s->kind == DOC_MESH) editMeshFree(&s->mesh);
    else                     entListSnapFree(&s->ents);
}

static void docHistInit(DocHistory *h) { h->nUndo = 0; h->nRedo = 0; }

static void docHistClearRedo(DocHistory *h)
{
    int i;
    for (i = 0; i < h->nRedo; i++) docSnapFree(&h->redo[i]);
    h->nRedo = 0;
}

static void docHistFree(DocHistory *h)
{
    int i;
    for (i = 0; i < h->nUndo; i++) docSnapFree(&h->undo[i]);
    for (i = 0; i < h->nRedo; i++) docSnapFree(&h->redo[i]);
    h->nUndo = h->nRedo = 0;
}

static void docHistDropOldest(DocHistory *h)
{
    int i;
    docSnapFree(&h->undo[0]);
    for (i = 1; i < h->nUndo; i++) h->undo[i - 1] = h->undo[i];
    h->nUndo--;
}

/* Push a mesh / entity restore point. Call BEFORE the mutating op; clears redo. */
static void docPushMesh(DocHistory *h, const EditMesh *m)
{
    docHistClearRedo(h);
    if (h->nUndo == EDIT_UNDO_MAX) docHistDropOldest(h);
    DocSnap *s = &h->undo[h->nUndo++];
    s->kind = DOC_MESH;
    editMeshCopy(&s->mesh, m);
}

static void docPushEnts(DocHistory *h, const EntityList *l)
{
    docHistClearRedo(h);
    if (h->nUndo == EDIT_UNDO_MAX) docHistDropOldest(h);
    DocSnap *s = &h->undo[h->nUndo++];
    s->kind = DOC_ENTS;
    entListSnapCapture(&s->ents, l);
}

/* Discard the most recent undo point (e.g. a cancelled grab already reverted). */
static void docDropUndoTop(DocHistory *h)
{
    if (h->nUndo > 0) docSnapFree(&h->undo[--h->nUndo]);
}

/* Undo the most recent edit: stash the CURRENT state of the affected document on
   redo, restore the snapshot. Returns the kind restored (DOC_MESH/DOC_ENTS) so
   the caller can reconcile the right document, or -1 if the undo stack is empty. */
static int docUndo(DocHistory *h, EditMesh *mesh, EntityList *ents)
{
    if (h->nUndo == 0) return -1;
    DocSnap *top = &h->undo[h->nUndo - 1];
    DocSnap *r   = &h->redo[h->nRedo++];
    int kind = top->kind;
    if (kind == DOC_MESH) {
        r->kind = DOC_MESH; editMeshCopy(&r->mesh, mesh);
        editMeshFree(mesh); *mesh = top->mesh;         /* transfer ownership */
    } else {
        r->kind = DOC_ENTS; entListSnapCapture(&r->ents, ents);
        entListSnapRestore(ents, &top->ents);
        entListSnapFree(&top->ents);
    }
    h->nUndo--;
    return kind;
}

static int docRedo(DocHistory *h, EditMesh *mesh, EntityList *ents)
{
    if (h->nRedo == 0) return -1;
    DocSnap *top = &h->redo[h->nRedo - 1];
    DocSnap *u   = &h->undo[h->nUndo++];
    int kind = top->kind;
    if (kind == DOC_MESH) {
        u->kind = DOC_MESH; editMeshCopy(&u->mesh, mesh);
        editMeshFree(mesh); *mesh = top->mesh;
    } else {
        u->kind = DOC_ENTS; entListSnapCapture(&u->ents, ents);
        entListSnapRestore(ents, &top->ents);
        entListSnapFree(&top->ents);
    }
    h->nRedo--;
    return kind;
}

/* Restore the top undo snapshot (mesh only) with NO redo entry — aborts an
   in-progress op, e.g. cancel an extrude+move as one step. Returns 0 if the top
   isn't a mesh snapshot (shouldn't happen: extrude only pushes mesh). */
static int docPopRestoreMesh(DocHistory *h, EditMesh *mesh)
{
    if (h->nUndo == 0 || h->undo[h->nUndo - 1].kind != DOC_MESH) return 0;
    editMeshFree(mesh);
    *mesh = h->undo[--h->nUndo].mesh;
    return 1;
}

static int docHasUndo(const DocHistory *h) { return h->nUndo > 0; }
static int docHasRedo(const DocHistory *h) { return h->nRedo > 0; }

#endif /* EDIT_HISTORY_H */
