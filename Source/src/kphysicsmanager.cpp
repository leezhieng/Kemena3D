#include "kphysicsmanager.h"

#ifdef _MSC_VER
#  pragma warning(push, 0)
#endif
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// Object layer constants  (must match kphysicsobject.cpp)
// ---------------------------------------------------------------------------
namespace Layers
{
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING     = 1;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}

// ---------------------------------------------------------------------------
// Broad-phase layer constants
// ---------------------------------------------------------------------------
namespace BPLayers
{
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr JPH::uint NUM_LAYERS = 2;
}

// ---------------------------------------------------------------------------
// Broad-phase layer interface implementation
// ---------------------------------------------------------------------------
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
    BPLayerInterfaceImpl()
    {
        m_objectToBroadPhase[Layers::NON_MOVING] = BPLayers::NON_MOVING;
        m_objectToBroadPhase[Layers::MOVING]     = BPLayers::MOVING;
    }

    JPH::uint GetNumBroadPhaseLayers() const override
    {
        return BPLayers::NUM_LAYERS;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        JPH_ASSERT(layer < Layers::NUM_LAYERS);
        return m_objectToBroadPhase[layer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        switch ((JPH::BroadPhaseLayer::Type)layer)
        {
            case (JPH::BroadPhaseLayer::Type)BPLayers::NON_MOVING: return "NON_MOVING";
            case (JPH::BroadPhaseLayer::Type)BPLayers::MOVING:     return "MOVING";
            default: JPH_ASSERT(false); return "INVALID";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer m_objectToBroadPhase[Layers::NUM_LAYERS];
};

// ---------------------------------------------------------------------------
// Object vs broad-phase layer filter
// ---------------------------------------------------------------------------
class ObjVsBPLayerFilter : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer layer1,
                       JPH::BroadPhaseLayer layer2) const override
    {
        switch (layer1)
        {
            case Layers::NON_MOVING: return layer2 == BPLayers::MOVING;
            case Layers::MOVING:     return true;
            default: return false;
        }
    }
};

// ---------------------------------------------------------------------------
// Object layer pair filter
// ---------------------------------------------------------------------------
class ObjLayerPairFilter : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer layer1,
                       JPH::ObjectLayer layer2) const override
    {
        switch (layer1)
        {
            case Layers::NON_MOVING: return layer2 == Layers::MOVING;
            case Layers::MOVING:     return true;
            default: return false;
        }
    }
};

// ---------------------------------------------------------------------------
// kPhysicsManager::Impl
// ---------------------------------------------------------------------------
namespace kemena
{
    struct kPhysicsManager::Impl
    {
        std::unique_ptr<JPH::TempAllocatorImpl>    tempAllocator;
        std::unique_ptr<JPH::JobSystemThreadPool>  jobSystem;
        std::unique_ptr<BPLayerInterfaceImpl>      bpLayerInterface;
        std::unique_ptr<ObjVsBPLayerFilter>        ovbpFilter;
        std::unique_ptr<ObjLayerPairFilter>        olpFilter;
        std::unique_ptr<JPH::PhysicsSystem>        physicsSystem;

        std::vector<kPhysicsObject *>              objects;
        std::vector<kCharacterController *>        characters;
        bool                                       initialized = false;

        static constexpr JPH::uint cMaxBodies             = 65536;
        static constexpr JPH::uint cNumBodyMutexes        = 0;
        static constexpr JPH::uint cMaxBodyPairs          = 65536;
        static constexpr JPH::uint cMaxContactConstraints = 10240;
    };

    // -----------------------------------------------------------------------
    // Constructor / destructor
    // -----------------------------------------------------------------------

    kPhysicsManager::kPhysicsManager()
        : m_impl(new Impl())
    {
    }

    kPhysicsManager::~kPhysicsManager()
    {
        shutdown();
        delete m_impl;
    }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    bool kPhysicsManager::init()
    {
        if (m_impl->initialized)
            return true;

        // Global Jolt initialisation (safe to call multiple times)
        JPH::RegisterDefaultAllocator();

        if (!JPH::Factory::sInstance)
        {
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        }

        // 10 MB scratch allocator for the physics engine
        m_impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

        // Thread-pool job system (leave 1 core for the game thread)
        const int workerCount = std::max(1,
            static_cast<int>(std::thread::hardware_concurrency()) - 1);
        m_impl->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workerCount);

        // Collision layer infrastructure
        m_impl->bpLayerInterface = std::make_unique<BPLayerInterfaceImpl>();
        m_impl->ovbpFilter       = std::make_unique<ObjVsBPLayerFilter>();
        m_impl->olpFilter        = std::make_unique<ObjLayerPairFilter>();

        // Physics world
        m_impl->physicsSystem = std::make_unique<JPH::PhysicsSystem>();
        m_impl->physicsSystem->Init(
            Impl::cMaxBodies,
            Impl::cNumBodyMutexes,
            Impl::cMaxBodyPairs,
            Impl::cMaxContactConstraints,
            *m_impl->bpLayerInterface,
            *m_impl->ovbpFilter,
            *m_impl->olpFilter);

        // Default gravity: 9.81 m/s² downward
        m_impl->physicsSystem->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

        m_impl->initialized = true;
        return true;
    }

    void kPhysicsManager::shutdown()
    {
        if (!m_impl->initialized)
            return;

        // Destroy all tracked character controllers first (they hold bodies too)
        for (kCharacterController *cc : m_impl->characters)
        {
            cc->uninit();
            delete cc;
        }
        m_impl->characters.clear();

        // Destroy all tracked objects (removes bodies from the simulation)
        for (kPhysicsObject *obj : m_impl->objects)
        {
            obj->uninit();
            delete obj;
        }
        m_impl->objects.clear();

        // Tear down Jolt systems in reverse order
        m_impl->physicsSystem.reset();
        m_impl->jobSystem.reset();
        m_impl->tempAllocator.reset();
        m_impl->olpFilter.reset();
        m_impl->ovbpFilter.reset();
        m_impl->bpLayerInterface.reset();

        // Release type registry (only if this is the last manager)
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;

        m_impl->initialized = false;
    }

    // -----------------------------------------------------------------------
    // Simulation
    // -----------------------------------------------------------------------

    void kPhysicsManager::update(float deltaTime)
    {
        if (!m_impl->initialized || deltaTime <= 0.0f)
            return;

        // 1 collision step is fine for games running ≥ 30 fps
        const int cCollisionSteps = 1;
        m_impl->physicsSystem->Update(
            deltaTime,
            cCollisionSteps,
            m_impl->tempAllocator.get(),
            m_impl->jobSystem.get());

        // Refresh each character's ground/contact state after the world step.
        for (kCharacterController *cc : m_impl->characters)
            cc->update(deltaTime);
    }

    // -----------------------------------------------------------------------
    // World settings
    // -----------------------------------------------------------------------

    void kPhysicsManager::setGravity(const kVec3 &gravity)
    {
        if (!m_impl->initialized) return;
        m_impl->physicsSystem->SetGravity(
            JPH::Vec3(gravity.x, gravity.y, gravity.z));
    }

    kVec3 kPhysicsManager::getGravity() const
    {
        if (!m_impl->initialized) return kVec3(0.0f, -9.81f, 0.0f);
        JPH::Vec3 g = m_impl->physicsSystem->GetGravity();
        return kVec3(g.GetX(), g.GetY(), g.GetZ());
    }

    // -----------------------------------------------------------------------
    // Object factory
    // -----------------------------------------------------------------------

    kPhysicsObject *kPhysicsManager::createObject(const kPhysicsObjectDesc &desc)
    {
        if (!m_impl->initialized)
        {
            std::cout << "[kPhysicsManager] createObject called before init()." << std::endl;
            return nullptr;
        }

        kPhysicsObject *obj = new kPhysicsObject();
        if (!obj->init(m_impl->physicsSystem.get(), desc))
        {
            delete obj;
            return nullptr;
        }

        m_impl->objects.push_back(obj);
        return obj;
    }

    void kPhysicsManager::destroyObject(kPhysicsObject *object)
    {
        if (!object) return;

        auto it = std::find(m_impl->objects.begin(), m_impl->objects.end(), object);
        if (it != m_impl->objects.end())
            m_impl->objects.erase(it);

        object->uninit();
        delete object;
    }

    kCharacterController *kPhysicsManager::createCharacter(const kCharacterControllerDesc &desc)
    {
        if (!m_impl->initialized)
        {
            std::cout << "[kPhysicsManager] createCharacter called before init()." << std::endl;
            return nullptr;
        }

        kCharacterController *cc = new kCharacterController();
        if (!cc->init(m_impl->physicsSystem.get(), desc))
        {
            delete cc;
            return nullptr;
        }

        m_impl->characters.push_back(cc);
        return cc;
    }

    void kPhysicsManager::destroyCharacter(kCharacterController *character)
    {
        if (!character) return;

        auto it = std::find(m_impl->characters.begin(), m_impl->characters.end(), character);
        if (it != m_impl->characters.end())
            m_impl->characters.erase(it);

        character->uninit();
        delete character;
    }

    // -----------------------------------------------------------------------
    // Queries
    // -----------------------------------------------------------------------

    kPhysicsRaycastHit kPhysicsManager::raycast(const kVec3 &origin,
                                                 const kVec3 &direction,
                                                 float maxDistance)
    {
        kPhysicsRaycastHit result;
        if (!m_impl->initialized) return result;

        JPH::RRayCast ray{
            JPH::RVec3(origin.x, origin.y, origin.z),
            JPH::Vec3(direction.x, direction.y, direction.z) * maxDistance
        };

        JPH::RayCastResult hit;
        if (!m_impl->physicsSystem->GetNarrowPhaseQuery().CastRay(ray, hit))
            return result;

        result.hit      = true;
        result.distance = hit.mFraction * maxDistance;
        result.hitPoint = origin + direction * result.distance;

        // Retrieve surface normal via a body read lock.
        {
            JPH::BodyLockRead lock(m_impl->physicsSystem->GetBodyLockInterface(),
                                   hit.mBodyID);
            if (lock.Succeeded())
            {
                const JPH::Body &body = lock.GetBody();
                JPH::Vec3 normal = body.GetWorldSpaceSurfaceNormal(
                    hit.mSubShapeID2,
                    JPH::RVec3(result.hitPoint.x, result.hitPoint.y, result.hitPoint.z));
                result.hitNormal = kVec3(normal.GetX(), normal.GetY(), normal.GetZ());
            }
        }

        // Map the Jolt BodyID back to the kPhysicsObject owned by this manager.
        uint32_t bodyIdVal = hit.mBodyID.GetIndexAndSequenceNumber();
        for (kPhysicsObject *obj : m_impl->objects)
        {
            if (obj->getBodyId() == bodyIdVal)
            {
                result.object = obj;
                break;
            }
        }

        return result;
    }

} // namespace kemena
