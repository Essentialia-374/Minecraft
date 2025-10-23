#pragma once
#include <PxPhysicsAPI.h>

// Seriously, don't spread PhysX globals everywhere. One owner, everybody else borrows.
namespace Minecraft
{
    class PhysXSystem
    {
    public:
        static PhysXSystem& Instance();

        // Call once, early. `enablePvd` optional. Use it if you like shiny graphs.
        void Initialize(bool enablePvd = false);
        void Shutdown();

        physx::PxPhysics& Physics();
        physx::PxScene& Scene();
        physx::PxControllerManager& Controllers();
        physx::PxMaterial& DefaultMaterial();

        // Drive the scene. Fixed or variable step, your call. Just don't skip fetchResults.
        void Step(float dt);

    private:
        PhysXSystem() = default;
        ~PhysXSystem() = default;
        PhysXSystem(const PhysXSystem&) = delete;
        PhysXSystem& operator=(const PhysXSystem&) = delete;

        physx::PxDefaultAllocator      m_Allocator;
        physx::PxDefaultErrorCallback  m_ErrorCallback;

        physx::PxFoundation* m_Foundation = nullptr;
        physx::PxPhysics* m_Physics = nullptr;
        physx::PxPvd* m_Pvd = nullptr;
        physx::PxPvdTransport* m_PvdTransport = nullptr;
        physx::PxDefaultCpuDispatcher* m_Dispatcher = nullptr;
        physx::PxScene* m_Scene = nullptr;
        physx::PxControllerManager* m_ControllerMgr = nullptr;
        physx::PxMaterial* m_DefaultMat = nullptr;
    };
}