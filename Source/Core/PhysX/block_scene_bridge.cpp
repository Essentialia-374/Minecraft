#include "block_scene_bridge.h"
#include "../Player/Player.h" // for GetWorldBlock declaration (yeah, extern; fix later)
#include <PxPhysicsAPI.h>

using namespace physx;

namespace Minecraft
{
    extern Block* GetWorldBlock(const glm::vec3& block_pos); // defined elsewhere

    BlockSceneBridge::BlockSceneBridge(PxPhysics& physics, PxScene& scene, PxMaterial& mat)
        : m_Physics(physics), m_Scene(scene), m_Material(mat) {
    }

    bool BlockSceneBridge::SolidAt(int x, int y, int z) const
    {
        Block* b = GetWorldBlock(glm::vec3(x, y, z));
        return b && b->Collidable();
    }

    void BlockSceneBridge::EnsureBlock(int x, int y, int z, bool solid)
    {
        Key key{ x,y,z };
        auto it = m_Actors.find(key);
        if (solid)
        {
            if (it != m_Actors.end()) return; // already there
            PxTransform T(PxVec3(x + 0.5f, y + 0.5f, z + 0.5f));
            PxRigidStatic* actor = m_Physics.createRigidStatic(T);
            PxShape* shape = PxRigidActorExt::createExclusiveShape(*actor, PxBoxGeometry(0.5f, 0.5f, 0.5f), m_Material);
            (void)shape; // not your business
            m_Scene.addActor(*actor);
            m_Actors.emplace(key, actor);
        }
        else
        {
            if (it == m_Actors.end()) return;
            PxRigidStatic* a = it->second;
            m_Scene.removeActor(*a);
            a->release();
            m_Actors.erase(it);
        }
    }

    bool BlockSceneBridge::InCurrentWindow(const Key& k) const
    {
        return (k.x >= m_LastCenter.x - m_LastHalf.x && k.x <= m_LastCenter.x + m_LastHalf.x) &&
            (k.y >= m_LastCenter.y - m_LastHalf.y && k.y <= m_LastCenter.y + m_LastHalf.y) &&
            (k.z >= m_LastCenter.z - m_LastHalf.z && k.z <= m_LastCenter.z + m_LastHalf.z);
    }

    void BlockSceneBridge::UpdateNeighborhood(const glm::ivec3& center, const glm::ivec3& halfExtents)
    {
        // Only rebuild when we cross block boundaries or extents change. Anything else is churn.
        if (center == m_LastCenter && halfExtents == m_LastHalf) return;

        // Prune actors that drifted outside the new window (don't keep ancient trash around).
        for (auto it = m_Actors.begin(); it != m_Actors.end(); )
        {
            if (!InCurrentWindow(it->first))
            {
                PxRigidStatic* a = it->second;
                m_Scene.removeActor(*a);
                a->release();
                it = m_Actors.erase(it);
            }
            else ++it;
        }

        // Add/remove within the window
        for (int x = center.x - halfExtents.x; x <= center.x + halfExtents.x; ++x)
            for (int y = center.y - halfExtents.y; y <= center.y + halfExtents.y; ++y)
                for (int z = center.z - halfExtents.z; z <= center.z + halfExtents.z; ++z)
                {
                    EnsureBlock(x, y, z, SolidAt(x, y, z));
                }

        m_LastCenter = center;
        m_LastHalf = halfExtents;
    }

    void BlockSceneBridge::Clear()
    {
        for (auto& kv : m_Actors)
        {
            PxRigidStatic* a = kv.second;
            m_Scene.removeActor(*a);
            a->release();
        }
        m_Actors.clear();
        m_LastCenter = glm::ivec3(INT32_MIN, INT32_MIN, INT32_MIN);
        m_LastHalf = glm::ivec3(0);
    }
}