#pragma once
#include <glm/glm.hpp>
#include <PxPhysicsAPI.h>

inline physx::PxVec3 ToPx(const glm::vec3& v) { return physx::PxVec3(v.x, v.y, v.z); }
inline glm::vec3 ToGlm(const physx::PxVec3& v) { return glm::vec3(v.x, v.y, v.z); }
inline glm::vec3 ToGlm(const physx::PxExtendedVec3& v) { return glm::vec3((float)v.x, (float)v.y, (float)v.z); }