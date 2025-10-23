#include "Player.h"
#include <glm/gtc/constants.hpp>

namespace Minecraft
{
    // yeah yeah, externs… they existed before. Keep the ABI stable for now.
    extern float ex_PlayerSpeed;
    extern float ex_PlayerSensitivity;

    using namespace physx;

    void Player::AttachToPhysics(const glm::vec3& spawn)
    {
        auto& px = PhysXSystem::Instance();

        // Capsule dims: full height = height + 2*radius. We want 1.8 total, width 0.75 => radius=0.375, cylinder=1.05
        const float radius = PLAYER_WIDTH * 0.5f;
        const float cyl = PLAYER_HEIGHT - 2.0f * radius;

        PxCapsuleControllerDesc desc;
        desc.height = cyl;                // cylinder portion
        desc.radius = radius;
        desc.contactOffset = 0.05f;
        desc.stepOffset = STEP_OFFSET;
        desc.slopeLimit = SLOPE_LIMIT_COS;
        desc.density = 0.0f;              // kinematic
        desc.material = &px.DefaultMaterial();
        desc.position = PxExtendedVec3(spawn.x, spawn.y - EYE_LEVEL, spawn.z); // foot pos
        desc.upDirection = PxVec3(0, 1, 0);
        desc.nonWalkableMode = PxControllerNonWalkableMode::ePREVENT_CLIMBING_AND_FORCE_SLIDING;

        m_Controller = px.Controllers().createController(desc);
        if (!m_Controller) throw std::runtime_error("PhysX: failed to create CapsuleController");

        // Start camera exactly above the controller feet, at eye height.
        p_Position = spawn;
        *(glm::vec3*)(&p_Camera.GetPosition()) = p_Position; // yes, still ugly. Fix your camera API later.
        p_Camera.Refresh();

        // Voxel bridge window — don't be silly big. Keep it tight and cheap.
        m_BlockBridge = std::make_unique<BlockSceneBridge>(px.Physics(), px.Scene(), px.DefaultMaterial());
        m_BlockBridge->UpdateNeighborhood(glm::ivec3((int)floor(spawn.x), (int)floor(spawn.y), (int)floor(spawn.z)),
            glm::ivec3(12, 8, 12));

        m_TimeLast = glfwGetTime();
    }

    glm::vec3 Player::ComputeWalkDir() const
    {
        // World-space forward on XZ plane from camera yaw. Keep it simple. No magic "acceleration".
        float yaw = glm::radians(p_Camera.GetYaw());
        glm::vec3 walk_forward = glm::vec3(cosf(yaw), 0.0f, sinf(yaw));
        glm::vec3 walk_right = glm::vec3(-sinf(yaw), 0.0f, cosf(yaw));
        return glm::normalize(walk_forward) + glm::normalize(walk_right) * 0.0f; // placeholder; caller composes
    }

    void Player::UpdateFreeFly(GLFWwindow* window, float camera_speed)
    {
        p_Camera.ResetAcceleration();

        if (p_Camera.GetSensitivity() != ex_PlayerSensitivity)
            p_Camera.SetSensitivity(ex_PlayerSensitivity);

        glm::vec3 forward = -glm::cross(p_Camera.GetRight(), p_Camera.GetUp());
        glm::vec3 back = -forward;
        glm::vec3 right = p_Camera.GetRight();

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) p_Camera.ApplyAcceleration(forward * camera_speed);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) p_Camera.ApplyAcceleration(back * camera_speed);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) p_Camera.ApplyAcceleration(-(right * camera_speed));
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) p_Camera.ApplyAcceleration(right * camera_speed);
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)      p_Camera.ApplyAcceleration(p_Camera.GetUp() * camera_speed);
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) p_Camera.ApplyAcceleration(-(p_Camera.GetUp() * camera_speed));

        p_Camera.OnUpdate();
        p_Position = p_Camera.GetPosition();
    }

    void Player::UpdatePhysX(GLFWwindow* window, float camera_speed, float dt)
    {
        auto& px = PhysXSystem::Instance();

        // Keep the bridge window centered. We only rebuild on block-boundary move.
        glm::vec3 camPos = p_Camera.GetPosition();
        glm::ivec3 center{ (int)floor(camPos.x), (int)floor(camPos.y - EYE_LEVEL), (int)floor(camPos.z) };
        m_BlockBridge->UpdateNeighborhood(center, glm::ivec3(12, 8, 12));

        // Input → planar move (XZ). No more “camera acceleration”. You want speed — move.
        glm::vec3 move(0.0f);
        {
            float yaw = glm::radians(p_Camera.GetYaw());
            const glm::vec3 walk_forward = glm::vec3(cosf(yaw), 0.0f, sinf(yaw));
            const glm::vec3 walk_right = glm::vec3(-sinf(yaw), 0.0f, cosf(yaw));
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) 
                move += walk_forward;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) 
                move -= walk_forward;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
                move -= walk_right;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
                move += walk_right;
            if (glm::dot(move, move) > 0.0001f)
                move = glm::normalize(move) * camera_speed;
        }

        // Jump/gravity — actual vertical velocity, not twitching the camera Y.
        constexpr float gravity = -20.0f;   // blocks/s^2. More snappy than Earth g in “block units”.
        constexpr float jump_v = 6.0f;    // tune to taste

        if (m_JumpRequested && m_IsOnGround)
        {
            m_VelY = jump_v;
            m_IsOnGround = false;
        }
        m_JumpRequested = false;

        m_VelY += gravity * dt;

        // Apply via CCT
        PxControllerFilters filters;
        const PxVec3 disp(move.x * dt, m_VelY * dt, move.z * dt);
        const PxU32 flags = m_Controller->move(disp, 0.001f, dt, filters);

        m_IsOnGround = (flags & PxControllerCollisionFlag::eCOLLISION_DOWN);
        if (m_IsOnGround && m_VelY < 0.0f) m_VelY = 0.0f;
        if (flags & PxControllerCollisionFlag::eCOLLISION_UP) { if (m_VelY > 0.0f) m_VelY = 0.0f; }

        // Sync camera to controller (feet -> eye)
        glm::vec3 feet = ToGlm(m_Controller->getFootPosition());
        glm::vec3 eye = feet; eye.y += EYE_LEVEL;
        *(glm::vec3*)(&p_Camera.GetPosition()) = eye;
        p_Camera.Refresh();
        p_Position = eye;

        // Step physics scene once per frame — this isn’t a physics tech demo. Keep it simple.
        px.Step(dt);
    }

    void Player::OnUpdate(GLFWwindow* window)
    {
        const float camera_speed = ex_PlayerSpeed;

        if (p_Camera.GetSensitivity() != ex_PlayerSensitivity)
            p_Camera.SetSensitivity(ex_PlayerSensitivity);

        // Time step — if your engine already tracks dt, use that. This is the least stupid fallback.
        const double tNow = glfwGetTime();
        float dt = (float)(tNow - m_TimeLast);
        if (dt <= 0.0f) dt = 1.0f / 60.0f;
        if (dt > 0.1f)  dt = 0.1f; // don't explode on stalls
        m_TimeLast = tNow;

        if (p_FreeFly)
        {
            // No physics. Move camera, be happy.
            UpdateFreeFly(window, camera_speed);
            m_IsOnGround = false;
            m_JumpRequested = false;
        }
        else
        {
            if (!m_Controller)
            {
                // What the hell are you doing calling OnUpdate before AttachToPhysics?
                // Fine. Boot a default controller at current position.
                AttachToPhysics(p_Camera.GetPosition());
            }
            UpdatePhysX(window, camera_speed, dt);
        }
    }

    void Player::OnEvent(EventSystem::Event e)
    {
        if (e.type == EventSystem::EventTypes::MouseScroll)
        {
            if (e.msy > 0.0f) { if (p_Camera.GetFov() < 71.0f)   p_Camera.SetFov(p_Camera.GetFov() + 0.1f); }
            else if (e.msy < 0.0f) { if (p_Camera.GetFov() > 69.50f)  p_Camera.SetFov(p_Camera.GetFov() - 0.1f); }
        }
        else if (e.type == EventSystem::EventTypes::KeyPress)
        {
            if (e.key == GLFW_KEY_F)
            {
                p_FreeFly = !p_FreeFly;
            }
            else if (e.key == GLFW_KEY_SPACE)
            {
                if (!p_FreeFly) m_JumpRequested = true;
            }
        }
        else if (e.type == EventSystem::EventTypes::MouseMove)
        {
            p_Camera.UpdateOnMouseMovement(e.mx, e.my);
        }
    }

    // Legacy method — now uses PhysX overlap so callers don't break.
    bool Player::TestBlockCollision(const glm::vec3& position)
    {
        if (p_FreeFly || !m_Controller) return false;

        auto& scene = PhysXSystem::Instance().Scene();

        const float radius = PLAYER_WIDTH * 0.5f;
        const float cyl = PLAYER_HEIGHT - 2.0f * radius;
        const float halfCyl = 0.5f * cyl;

        // Position is eye. Convert to capsule center (feet + radius + halfCyl).
        float footY = position.y - EYE_LEVEL;
        float centerY = footY + radius + halfCyl;

        PxCapsuleGeometry capsule(radius, halfCyl);
        PxTransform pose(PxVec3(position.x, centerY, position.z));

        PxOverlapBuffer buf;
        PxQueryFilterData qfd(PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC);

        // Ignore self if needed
        struct NoSelf final : PxQueryFilterCallback {
            PxRigidActor* self;
            explicit NoSelf(PxRigidActor* a) : self(a) {}

            PxQueryHitType::Enum preFilter(const PxFilterData&,
                const PxShape* /*shape*/,
                const PxRigidActor* actor,
                PxHitFlags& /*queryFlags*/) override
            {
                return (actor == self) ? PxQueryHitType::eNONE : PxQueryHitType::eBLOCK;
            }
            PxQueryHitType::Enum postFilter(const PxFilterData&,
                const PxQueryHit&,
                const PxShape*,
                const PxRigidActor*) override
            {
                return PxQueryHitType::eBLOCK;
            }
        } cb(m_Controller ? m_Controller->getActor() : nullptr);


        bool hit = scene.overlap(capsule, pose, buf, qfd, &cb);
        return hit;
    }
}
