#include "physx_system.h"
#include <stdexcept>
using namespace physx;

namespace Minecraft
{
    PhysXSystem& PhysXSystem::Instance()
    {
        static PhysXSystem s;
        return s;
    }

    void PhysXSystem::Initialize(bool enablePvd)
    {
        if (m_Foundation) return; // already up; don't be clever.

        m_Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_Allocator, m_ErrorCallback);
        if (!m_Foundation) throw std::runtime_error("PhysX: PxCreateFoundation failed");

        if (enablePvd)
        {
            m_Pvd = PxCreatePvd(*m_Foundation);
            m_PvdTransport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
            if (m_Pvd && m_PvdTransport) m_Pvd->connect(*m_PvdTransport, PxPvdInstrumentationFlag::eALL);
        }

        PxTolerancesScale scale;
        m_Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_Foundation, scale, true, m_Pvd);
        if (!m_Physics) throw std::runtime_error("PhysX: PxCreatePhysics failed");

        PxSceneDesc sceneDesc(m_Physics->getTolerancesScale());
        sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
        m_Dispatcher = PxDefaultCpuDispatcherCreate(2);
        sceneDesc.cpuDispatcher = m_Dispatcher;
        sceneDesc.filterShader = PxDefaultSimulationFilterShader;
        m_Scene = m_Physics->createScene(sceneDesc);
        if (!m_Scene) throw std::runtime_error("PhysX: createScene failed");

        m_ControllerMgr = PxCreateControllerManager(*m_Scene);
        if (!m_ControllerMgr) throw std::runtime_error("PhysX: ControllerManager failed");

        m_DefaultMat = m_Physics->createMaterial(0.6f, 0.6f, 0.0f); // friction sane, no bounce
    }

    void PhysXSystem::Shutdown()
    {
        if (m_ControllerMgr) { m_ControllerMgr->release(); m_ControllerMgr = nullptr; }
        if (m_Scene) { m_Scene->release();         m_Scene = nullptr; }
        if (m_Dispatcher) { m_Dispatcher->release();    m_Dispatcher = nullptr; }
        if (m_DefaultMat) { m_DefaultMat->release();    m_DefaultMat = nullptr; }
        if (m_Physics) { m_Physics->release();       m_Physics = nullptr; }
        if (m_Pvd)
        {
            if (m_Pvd->isConnected()) m_Pvd->disconnect();
            if (m_PvdTransport) { m_PvdTransport->release(); m_PvdTransport = nullptr; }
            m_Pvd->release(); m_Pvd = nullptr;
        }
        if (m_Foundation) { m_Foundation->release();    m_Foundation = nullptr; }
    }

    PxPhysics& PhysXSystem::Physics() { return *m_Physics; }
    PxScene& PhysXSystem::Scene() { return *m_Scene; }
    PxControllerManager& PhysXSystem::Controllers() { return *m_ControllerMgr; }
    PxMaterial& PhysXSystem::DefaultMaterial() { return *m_DefaultMat; }

    void PhysXSystem::Step(float dt)
    {
        if (!m_Scene) return;
        m_Scene->simulate(dt);
        m_Scene->fetchResults(true);
    }
}