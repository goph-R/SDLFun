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

static void physStep(PhysWorld *pw, float dt)
{
    pw->world->stepSimulation(dt, 4, 1.0f / 120.0f);
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
