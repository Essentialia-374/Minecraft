#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <PxPhysicsAPI.h>

#include "../FpsCamera.h"
#include "../Block.h"
#include "../Application/Events.h"
#include "../PhysX/physx_system.h"
#include "../PhysX/physx_utils.h"
#include "../PhysX/block_scene_bridge.h"

namespace Minecraft
{
    class World;

    // Stop pretending the camera is the physics body. It's not.
    class Player
    {
    public:
        Player(float wx, float wy)
            : p_Camera(70.0f, wx / wy, 0.1f, 500.0f)
        {
        }

        // Same API. Internals are sane now.
        void OnUpdate(GLFWwindow* window);
        void OnEvent(EventSystem::Event e);

        // Legacy hook. Now backed by a PhysX overlap on the capsule. Callers that still poke this: fine.
        bool TestBlockCollision(const glm::vec3& position);

        // PhysX hookup — call once after PhysXSystem::Initialize
        void AttachToPhysics(const glm::vec3& spawn);

        FPSCamera   p_Camera;
        glm::vec3   p_Position{ 0.0f };
        World* p_World = nullptr;
        std::uint8_t p_CurrentHeldBlock = 0;
        bool        p_IsColliding = false;
        bool        p_FreeFly = false;

    private:
        // CCT parameters — pick values that don't suck.
        static constexpr float PLAYER_HEIGHT = 1.8f;   // total height
        static constexpr float PLAYER_WIDTH = 0.75f;  // capsule diameter ~ width
        static constexpr float EYE_LEVEL = 1.6f;   // camera offset from feet
        static constexpr float STEP_OFFSET = 0.25f;  // step climb
        static constexpr float SLOPE_LIMIT_COS = 0.70710678f; // cos(45°)

        // Gravity/jump are velocities, not impulses duct-taped to the camera.
        float       m_VelY = 0.0f;
        bool        m_JumpRequested = false;
        bool        m_IsOnGround = false;

        physx::PxController* m_Controller = nullptr;
        std::unique_ptr<BlockSceneBridge> m_BlockBridge;

        // No, we don't store dt in globals. We just compute it.
        double      m_TimeLast = 0.0;

        // Helpers
        void UpdateFreeFly(GLFWwindow* window, float camera_speed);
        void UpdatePhysX(GLFWwindow* window, float camera_speed, float dt);
        glm::vec3  ComputeWalkDir() const;
    };
}
