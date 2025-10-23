#pragma once
#include <unordered_map>
#include <glm/glm.hpp>
#include <PxPhysicsAPI.h>
#include "../Block.h" 

namespace Minecraft
{
    // Yes, it's per-block static actors. In a tight window. It's fine for a first pass.
    class BlockSceneBridge
    {
    public:
        BlockSceneBridge(physx::PxPhysics& physics, physx::PxScene& scene, physx::PxMaterial& mat);

        // Keep a cube of blocks around the player mirrored into PhysX.
        void UpdateNeighborhood(const glm::ivec3& center, const glm::ivec3& halfExtents);

        void Clear();

    private:
        struct Key { int x, y, z; };
        struct KeyHash {
            size_t operator()(const Key& k) const noexcept {
                // mix, because collisions... are bad in hashing too
                auto h1 = std::hash<int>()(k.x);
                auto h2 = std::hash<int>()(k.y);
                auto h3 = std::hash<int>()(k.z);
                return ((h1 * 1315423911u) ^ (h2 << 11)) + (h3 * 2654435761u);
            }
        };
        struct KeyEq {
            bool operator()(const Key& a, const Key& b) const noexcept {
                return a.x == b.x && a.y == b.y && a.z == b.z;
            }
        };

        physx::PxPhysics& m_Physics;
        physx::PxScene& m_Scene;
        physx::PxMaterial& m_Material;

        std::unordered_map<Key, physx::PxRigidStatic*, KeyHash, KeyEq> m_Actors;
        glm::ivec3 m_LastCenter{ INT32_MIN, INT32_MIN, INT32_MIN };
        glm::ivec3 m_LastHalf{ 0,0,0 };

        void EnsureBlock(int x, int y, int z, bool solid);
        bool InCurrentWindow(const Key& k) const;
        bool SolidAt(int x, int y, int z) const;
    };
}