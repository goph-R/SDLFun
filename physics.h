#ifndef PHYSICS_H
#define PHYSICS_H

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletDynamics/Character/btKinematicCharacterController.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>

#include "obj_loader.h"

/* Thin subclass that exposes the protected m_verticalVelocity so the
   ceiling clamp in physStep() can cancel upward motion on impact. */
class FpsCharacterController : public btKinematicCharacterController {
public:
    FpsCharacterController(btPairCachingGhostObject *ghost,
                           btConvexShape *shape, btScalar stepHeight)
        : btKinematicCharacterController(ghost, shape, stepHeight) {}
    void zeroVerticalVelocity() { m_verticalVelocity = 0.0f; }
    btScalar getVerticalVelocity() const { return m_verticalVelocity; }
};

struct PhysWorld {
    btDefaultCollisionConfiguration *config;
    btCollisionDispatcher *dispatcher;
    btBroadphaseInterface *broadphase;
    btSequentialImpulseConstraintSolver *solver;
    btDiscreteDynamicsWorld *world;

    /* Level collision */
    btTriangleMesh *levelTriMesh;
    btBvhTriangleMeshShape *levelShape;
    btRigidBody *levelBody;

    /* Player */
    btPairCachingGhostObject *ghostObject;
    btCapsuleShape *capsuleShape;
    FpsCharacterController *character;
};

static void physInit(PhysWorld *pw)
{
    pw->config = new btDefaultCollisionConfiguration();
    pw->dispatcher = new btCollisionDispatcher(pw->config);
    pw->broadphase = new btDbvtBroadphase();
    /* Register ghost pair callback so character controller works */
    pw->broadphase->getOverlappingPairCache()->setInternalGhostPairCallback(
        new btGhostPairCallback());
    pw->solver = new btSequentialImpulseConstraintSolver();
    pw->world = new btDiscreteDynamicsWorld(
        pw->dispatcher, pw->broadphase, pw->solver, pw->config);
    pw->world->setGravity(btVector3(0, -9.81f, 0));

    pw->levelTriMesh = NULL;
    pw->levelShape = NULL;
    pw->levelBody = NULL;
    pw->ghostObject = NULL;
    pw->capsuleShape = NULL;
    pw->character = NULL;
}

static void physLoadLevel(PhysWorld *pw, ObjMesh *mesh)
{
    pw->levelTriMesh = new btTriangleMesh();

    for (int i = 0; i < mesh->numTris; i++) {
        Triangle *t = &mesh->tris[i];
        Vec3 *a = &mesh->verts[t->v[0]];
        Vec3 *b = &mesh->verts[t->v[1]];
        Vec3 *c = &mesh->verts[t->v[2]];
        pw->levelTriMesh->addTriangle(
            btVector3(a->x, a->y, a->z),
            btVector3(b->x, b->y, b->z),
            btVector3(c->x, c->y, c->z));
    }

    pw->levelShape = new btBvhTriangleMeshShape(pw->levelTriMesh, true);

    btTransform transform;
    transform.setIdentity();
    btDefaultMotionState *ms = new btDefaultMotionState(transform);
    btRigidBody::btRigidBodyConstructionInfo info(0.0f, ms, pw->levelShape);
    pw->levelBody = new btRigidBody(info);
    pw->levelBody->setFriction(1.0f);
    pw->world->addRigidBody(pw->levelBody);
}

static void physCreatePlayer(PhysWorld *pw, float x, float y, float z)
{
    float playerHeight = 1.75f;
    float playerRadius = 0.225f;  /* ~0.45m shoulder width */
    float capsuleHeight = playerHeight - 2.0f * playerRadius;

    pw->capsuleShape = new btCapsuleShape(playerRadius, capsuleHeight);

    /* Diagnostic: what Bullet actually sees */
    printf("[phys] capsule: radius=%.3f halfHeight=%.3f margin=%.3f upAxis=%d\n",
           (float)pw->capsuleShape->getRadius(),
           (float)pw->capsuleShape->getHalfHeight(),
           (float)pw->capsuleShape->getMargin(),
           pw->capsuleShape->getUpAxis());

    pw->ghostObject = new btPairCachingGhostObject();
    btTransform startTransform;
    startTransform.setIdentity();
    startTransform.setOrigin(btVector3(x, y, z));
    pw->ghostObject->setWorldTransform(startTransform);
    pw->ghostObject->setCollisionShape(pw->capsuleShape);
    pw->ghostObject->setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT);

    /* Step height: max obstacle height the controller auto-climbs without
       jumping. 0.35m is the classic value and fine now that the player is
       full 1.75m tall — the earlier 0.15m was compensating for the sideways
       capsule bug where the effective player was radius-height. */
    float stepHeight = 0.35f;
    pw->character = new FpsCharacterController(
        pw->ghostObject, pw->capsuleShape, stepHeight);
    /* Heavier-than-Earth gravity so falls feel snappy instead of floaty.
       Real 9.81 m/s² makes short drops (under ~3 m) feel like moon physics
       in an FPS context. ~2.5G matches the Quake/HL1 feel roughly. */
    pw->character->setGravity(btVector3(0, -24.0f, 0));

    /* CRITICAL workaround: btKinematicCharacterController's constructor sets
       m_up=(0,0,1) (Z-up) internally, then calls setUp(x=(1,0,0)) from the
       default 4th arg, and setGravity(0,-9.81,0) calls setUpVector(0,1,0).
       Each m_up change makes setUpVector() ROTATE the ghost object — once
       Z→X, once X→Y — leaving our Y-aligned capsule tipped over onto its
       side (along X). The capsule ends up with center at y≈radius instead
       of halfHeight, and the X-axis collision footprint is ~1.75m wide.
       We undo these rotations by resetting the ghost's orientation to
       identity after init. Our capsule is already Y-aligned, the world is
       Y-up, no rotation needed. */
    {
        btTransform xform = pw->ghostObject->getWorldTransform();
        xform.setRotation(btQuaternion(0, 0, 0, 1));
        pw->ghostObject->setWorldTransform(xform);
    }
    pw->character->setMaxSlope(btRadians(50.0f));
    /* Jump gives max rise = v²/(2g) = 49/48 ≈ 1.02m — plenty for a 0.66m
       desk, comfortably under a 3m ceiling. */
    pw->character->setJumpSpeed(7.0f);

    pw->world->addCollisionObject(pw->ghostObject,
        btBroadphaseProxy::CharacterFilter,
        btBroadphaseProxy::StaticFilter | btBroadphaseProxy::DefaultFilter);
    pw->world->addAction(pw->character);
}

static void physGetPlayerPos(PhysWorld *pw, float *x, float *y, float *z)
{
    btTransform t = pw->ghostObject->getWorldTransform();
    btVector3 p = t.getOrigin();
    *x = p.getX();
    *y = p.getY();
    *z = p.getZ();
}

/* Static box collider for decorations. hx/hy/hz are half-extents in the
   entity's local space; the body is rotated by rotY (degrees) around Y.
   Returns the rigid body as a void* so non-Bullet callers (entity.h) can
   hold onto it without pulling in Bullet headers.
   Pass the returned pointer to physRemoveStaticBox() at cleanup. */
static void *physAddStaticBox(PhysWorld *pw,
                              float cx, float cy, float cz,
                              float hx, float hy, float hz,
                              float rotY)
{
    btBoxShape *shape = new btBoxShape(btVector3(hx, hy, hz));
    btTransform tr;
    tr.setIdentity();
    tr.setOrigin(btVector3(cx, cy, cz));
    btQuaternion q;
    q.setRotation(btVector3(0, 1, 0), rotY * (btScalar)SIMD_PI / 180.0f);
    tr.setRotation(q);
    btDefaultMotionState *ms = new btDefaultMotionState(tr);
    btRigidBody::btRigidBodyConstructionInfo info(0.0f, ms, shape);
    btRigidBody *body = new btRigidBody(info);
    body->setFriction(0.8f);
    pw->world->addRigidBody(body);
    return (void *)body;
}

static void physRemoveStaticBox(PhysWorld *pw, void *bodyPtr)
{
    if (!bodyPtr) return;
    btRigidBody *body = (btRigidBody *)bodyPtr;
    pw->world->removeRigidBody(body);
    delete body->getMotionState();
    delete body->getCollisionShape();
    delete body;
}

/* Kinematic box — same as static but movable. Used for doors: mass 0, but
   CF_KINEMATIC_OBJECT so Bullet pushes dynamic/character bodies out of the
   way when we reposition it. Remove with physRemoveStaticBox (same teardown). */
static void *physAddKinematicBox(PhysWorld *pw,
                                 float cx, float cy, float cz,
                                 float hx, float hy, float hz,
                                 float rotY)
{
    btBoxShape *shape = new btBoxShape(btVector3(hx, hy, hz));
    btTransform tr;
    tr.setIdentity();
    tr.setOrigin(btVector3(cx, cy, cz));
    btQuaternion q;
    q.setRotation(btVector3(0, 1, 0), rotY * (btScalar)SIMD_PI / 180.0f);
    tr.setRotation(q);
    btDefaultMotionState *ms = new btDefaultMotionState(tr);
    btRigidBody::btRigidBodyConstructionInfo info(0.0f, ms, shape);
    btRigidBody *body = new btRigidBody(info);
    body->setFriction(0.8f);
    body->setCollisionFlags(body->getCollisionFlags()
                            | btCollisionObject::CF_KINEMATIC_OBJECT);
    body->setActivationState(DISABLE_DEACTIVATION);
    pw->world->addRigidBody(body);
    return (void *)body;
}

/* Convex-sweep from the body's current transform to (cx, cy, cz, rotY).
   If the sweep hits anything other than the body itself, return 0 without
   moving. Otherwise commit the new transform and return 1. Used by doors
   to "stop if can't move" semantics — on block, the caller keeps state
   and retries next tick. */
struct _PhysNotMeConvexCB : public btCollisionWorld::ClosestConvexResultCallback {
    const btCollisionObject *me;
    _PhysNotMeConvexCB(const btCollisionObject *m,
                       const btVector3 &from, const btVector3 &to)
        : btCollisionWorld::ClosestConvexResultCallback(from, to), me(m) {}
    virtual btScalar addSingleResult(btCollisionWorld::LocalConvexResult &r,
                                     bool normalInWorld) {
        if (r.m_hitCollisionObject == me) return btScalar(1.0);
        return btCollisionWorld::ClosestConvexResultCallback::addSingleResult(
            r, normalInWorld);
    }
};

static int physMoveKinematicBox(PhysWorld *pw, void *bodyPtr,
                                float cx, float cy, float cz, float rotY)
{
    if (!bodyPtr) return 0;
    btRigidBody *body = (btRigidBody *)bodyPtr;
    btConvexShape *shape = (btConvexShape *)body->getCollisionShape();

    btTransform from = body->getWorldTransform();
    btTransform to;
    to.setIdentity();
    to.setOrigin(btVector3(cx, cy, cz));
    btQuaternion q;
    q.setRotation(btVector3(0, 1, 0), rotY * (btScalar)SIMD_PI / 180.0f);
    to.setRotation(q);

    _PhysNotMeConvexCB cb(body, from.getOrigin(), to.getOrigin());
    pw->world->convexSweepTest(shape, from, to, cb);
    if (cb.hasHit()) return 0;

    ((btDefaultMotionState *)body->getMotionState())->setWorldTransform(to);
    body->setWorldTransform(to);
    return 1;
}

/* Static triangle-mesh collider for decorations. Accurate for concave
   shapes (desks with kneeholes, chairs, arches). Pass the entity's
   ObjMesh directly; we copy triangles into a btTriangleMesh, apply
   scale via setLocalScaling, and place/rotate with the rigid body
   transform. Uses a separate remove function so box and trimesh
   resources can be freed with the right delete calls. */
struct PhysTrimeshHandle {
    btRigidBody *body;
    btBvhTriangleMeshShape *shape;
    btTriangleMesh *triMesh;
};

static void *physAddStaticTrimesh(PhysWorld *pw, ObjMesh *mesh,
                                  float cx, float cy, float cz,
                                  float rotY, float scale)
{
    if (mesh->numTris == 0) return NULL;
    btTriangleMesh *tri = new btTriangleMesh();
    for (int i = 0; i < mesh->numTris; i++) {
        Triangle *t = &mesh->tris[i];
        Vec3 *a = &mesh->verts[t->v[0]];
        Vec3 *b = &mesh->verts[t->v[1]];
        Vec3 *c = &mesh->verts[t->v[2]];
        tri->addTriangle(btVector3(a->x, a->y, a->z),
                         btVector3(b->x, b->y, b->z),
                         btVector3(c->x, c->y, c->z));
    }
    btBvhTriangleMeshShape *shape = new btBvhTriangleMeshShape(tri, true);
    if (scale > 0.0f && scale != 1.0f)
        shape->setLocalScaling(btVector3(scale, scale, scale));

    btTransform tr;
    tr.setIdentity();
    tr.setOrigin(btVector3(cx, cy, cz));
    btQuaternion q;
    q.setRotation(btVector3(0, 1, 0), rotY * (btScalar)SIMD_PI / 180.0f);
    tr.setRotation(q);
    btDefaultMotionState *ms = new btDefaultMotionState(tr);
    btRigidBody::btRigidBodyConstructionInfo info(0.0f, ms, shape);
    btRigidBody *body = new btRigidBody(info);
    body->setFriction(0.8f);
    pw->world->addRigidBody(body);

    PhysTrimeshHandle *h = (PhysTrimeshHandle *)malloc(sizeof(PhysTrimeshHandle));
    h->body = body;
    h->shape = shape;
    h->triMesh = tri;
    return (void *)h;
}

/* Debug: draw wireframes of every non-player collider so we can visually
   verify collider position/orientation matches the rendered mesh. */
static void physDebugDrawColliders(PhysWorld *pw)
{
    btCollisionObjectArray &objs = pw->world->getCollisionObjectArray();
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glLineWidth(2.0f);
    for (int i = 0; i < objs.size(); i++) {
        btCollisionObject *obj = objs[i];
        if (obj == pw->ghostObject) continue;
        btCollisionShape *shape = obj->getCollisionShape();
        if (!shape) continue;

        btTransform tr = obj->getWorldTransform();
        btVector3 o = tr.getOrigin();
        btMatrix3x3 m = tr.getBasis();
        float gl[16] = {
            m[0][0], m[1][0], m[2][0], 0,
            m[0][1], m[1][1], m[2][1], 0,
            m[0][2], m[1][2], m[2][2], 0,
            o.x(),   o.y(),   o.z(),   1
        };

        glPushMatrix();
        glMultMatrixf(gl);

        int type = shape->getShapeType();
        if (type == BOX_SHAPE_PROXYTYPE) {
            btBoxShape *box = (btBoxShape *)shape;
            btVector3 e = box->getHalfExtentsWithoutMargin();
            float x = e.x(), y = e.y(), z = e.z();
            glColor3f(1.0f, 0.3f, 0.3f); /* red for box */
            glBegin(GL_LINES);
            /* 12 edges of a box */
            glVertex3f(-x,-y,-z); glVertex3f( x,-y,-z);
            glVertex3f( x,-y,-z); glVertex3f( x,-y, z);
            glVertex3f( x,-y, z); glVertex3f(-x,-y, z);
            glVertex3f(-x,-y, z); glVertex3f(-x,-y,-z);
            glVertex3f(-x, y,-z); glVertex3f( x, y,-z);
            glVertex3f( x, y,-z); glVertex3f( x, y, z);
            glVertex3f( x, y, z); glVertex3f(-x, y, z);
            glVertex3f(-x, y, z); glVertex3f(-x, y,-z);
            glVertex3f(-x,-y,-z); glVertex3f(-x, y,-z);
            glVertex3f( x,-y,-z); glVertex3f( x, y,-z);
            glVertex3f( x,-y, z); glVertex3f( x, y, z);
            glVertex3f(-x,-y, z); glVertex3f(-x, y, z);
            glEnd();
        } else if (type == TRIANGLE_MESH_SHAPE_PROXYTYPE) {
            /* Draw each triangle edge */
            btBvhTriangleMeshShape *tms = (btBvhTriangleMeshShape *)shape;
            btVector3 scale = tms->getLocalScaling();
            btStridingMeshInterface *mi = tms->getMeshInterface();
            const unsigned char *vbase;
            int numVerts;
            PHY_ScalarType vtype;
            int vstride;
            const unsigned char *ibase;
            int istride;
            int numFaces;
            PHY_ScalarType itype;
            int nsub = mi->getNumSubParts();
            glColor3f(0.3f, 1.0f, 0.3f); /* green for trimesh */
            glBegin(GL_LINES);
            for (int sp = 0; sp < nsub; sp++) {
                mi->getLockedReadOnlyVertexIndexBase(&vbase, numVerts, vtype,
                    vstride, &ibase, istride, numFaces, itype, sp);
                for (int f = 0; f < numFaces; f++) {
                    const int *idx = (const int *)(ibase + f * istride);
                    for (int e = 0; e < 3; e++) {
                        const float *a = (const float *)(vbase + idx[e]       * vstride);
                        const float *b = (const float *)(vbase + idx[(e+1)%3] * vstride);
                        glVertex3f(a[0]*scale.x(), a[1]*scale.y(), a[2]*scale.z());
                        glVertex3f(b[0]*scale.x(), b[1]*scale.y(), b[2]*scale.z());
                    }
                }
                mi->unLockReadOnlyVertexBase(sp);
            }
            glEnd();
        }
        glPopMatrix();
    }
    glLineWidth(1.0f);
    glEnable(GL_LIGHTING);
}

static void physRemoveStaticTrimesh(PhysWorld *pw, void *handlePtr)
{
    if (!handlePtr) return;
    PhysTrimeshHandle *h = (PhysTrimeshHandle *)handlePtr;
    pw->world->removeRigidBody(h->body);
    delete h->body->getMotionState();
    delete h->body;
    delete h->shape;
    delete h->triMesh;
    free(h);
}

/* Raycast from a point in a direction. Returns 1 if hit, fills hitPos.
   maxDist = maximum ray length. */
static int physRaycast(PhysWorld *pw, float fromX, float fromY, float fromZ,
                       float dirX, float dirY, float dirZ, float maxDist,
                       float *hitX, float *hitY, float *hitZ)
{
    btVector3 from(fromX, fromY, fromZ);
    btVector3 to(fromX + dirX * maxDist, fromY + dirY * maxDist, fromZ + dirZ * maxDist);

    btCollisionWorld::ClosestRayResultCallback ray(from, to);
    ray.m_collisionFilterMask = btBroadphaseProxy::StaticFilter;
    pw->world->rayTest(from, to, ray);

    if (ray.hasHit()) {
        *hitX = ray.m_hitPointWorld.getX();
        *hitY = ray.m_hitPointWorld.getY();
        *hitZ = ray.m_hitPointWorld.getZ();
        return 1;
    }
    return 0;
}

static void physStep(PhysWorld *pw, float dt)
{
    pw->world->stepSimulation(dt, 4, 1.0f / 120.0f);

    /* Ceiling penetration correction:
       Raycast upward from capsule center. If the ray hits geometry
       closer than the capsule half-height, the top of the capsule
       has punched through a surface -- push the player back down
       so the capsule top sits at the ceiling. Runs every frame,
       so it acts as a continuous clamp even if upward velocity persists. */
    btTransform t = pw->ghostObject->getWorldTransform();
    btVector3 origin = t.getOrigin();
    float halfHeight = pw->capsuleShape->getHalfHeight()
                     + pw->capsuleShape->getRadius();
    btVector3 rayFrom = origin;
    btVector3 rayTo   = origin + btVector3(0, halfHeight, 0);

    btCollisionWorld::ClosestRayResultCallback rayCallback(rayFrom, rayTo);
    rayCallback.m_collisionFilterGroup = btBroadphaseProxy::CharacterFilter;
    rayCallback.m_collisionFilterMask  = btBroadphaseProxy::StaticFilter;
    pw->world->rayTest(rayFrom, rayTo, rayCallback);

    if (rayCallback.hasHit()) {
        /* Push the capsule out along the hit normal far enough that Bullet's
           internal stepUp — which raises the capsule by stepHeight each
           substep while vv < 0 — can't immediately re-penetrate the ceiling.
           Without this buffer the next substep's stepDown sweep starts in
           ceiling contact (closestHitFraction=0 → no movement, vv zeroed),
           trapping the player against the ceiling. The buffer costs a small
           visual snap on contact (capsule drops ~stepHeight below ceiling)
           but keeps head-bonks robust: no sticking, no clipping through. */
        const float epsilon = 0.001f;
        btVector3 hitPoint  = rayCallback.m_hitPointWorld;
        btVector3 hitNormal = rayCallback.m_hitNormalWorld;
        float distToHit = (hitPoint - origin).length();
        float penetration = halfHeight - distToHit;
        if (penetration > 0.0f) {
            float buffer = pw->character->getStepHeight() + 0.05f;
            t.setOrigin(origin + hitNormal * (penetration + buffer));
            pw->ghostObject->setWorldTransform(t);
            /* Kill upward velocity on contact so gravity takes over.
               Falling velocity is left alone. */
            if (pw->character->getVerticalVelocity() > 0.0f)
                pw->character->zeroVerticalVelocity();
        }
    }
}

static void physCleanup(PhysWorld *pw)
{
    if (pw->character) {
        pw->world->removeAction(pw->character);
        delete pw->character;
    }
    if (pw->ghostObject) {
        pw->world->removeCollisionObject(pw->ghostObject);
        delete pw->ghostObject;
    }
    if (pw->capsuleShape) delete pw->capsuleShape;
    if (pw->levelBody) {
        pw->world->removeRigidBody(pw->levelBody);
        delete pw->levelBody->getMotionState();
        delete pw->levelBody;
    }
    if (pw->levelShape) delete pw->levelShape;
    if (pw->levelTriMesh) delete pw->levelTriMesh;

    delete pw->world;
    delete pw->solver;
    delete pw->broadphase;
    delete pw->dispatcher;
    delete pw->config;
}

#endif
