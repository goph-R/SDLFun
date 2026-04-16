#ifndef PHYSICS_H
#define PHYSICS_H

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletDynamics/Character/btKinematicCharacterController.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>

#include "obj_loader.h"

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
    btKinematicCharacterController *character;
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
    float playerRadius = 0.35f;
    float capsuleHeight = playerHeight - 2.0f * playerRadius;

    pw->capsuleShape = new btCapsuleShape(playerRadius, capsuleHeight);

    pw->ghostObject = new btPairCachingGhostObject();
    btTransform startTransform;
    startTransform.setIdentity();
    startTransform.setOrigin(btVector3(x, y, z));
    pw->ghostObject->setWorldTransform(startTransform);
    pw->ghostObject->setCollisionShape(pw->capsuleShape);
    pw->ghostObject->setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT);

    float stepHeight = 0.35f;
    pw->character = new btKinematicCharacterController(
        pw->ghostObject, pw->capsuleShape, stepHeight);
    pw->character->setGravity(btVector3(0, -9.81f, 0));
    pw->character->setMaxSlope(btRadians(50.0f));
    pw->character->setJumpSpeed(5.0f);

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
        float ceilingY = rayCallback.m_hitPointWorld.getY();
        t.setOrigin(btVector3(origin.getX(), ceilingY - halfHeight, origin.getZ()));
        pw->ghostObject->setWorldTransform(t);
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
