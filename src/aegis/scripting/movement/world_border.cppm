module;

#include <cfloat>

export module aegis.scripting.movement.world_border;

import std;
import aegis.math;
import aegis.scripting.script_base;
import aegis.scene.components;

namespace aegis::scripting
{
/// @brief Limits the locaton of the entity to the given limits.
/// @note Only clamps the location, the velocity is not affected.
class WorldBorder : public ScriptBase
{
public:
    /// @brief Constructs a WorldBorder.
    /// @param limits Positive values for the limits.
    WorldBorder(const glm::vec3& limits = glm::vec3{ FLT_MAX }) :
        m_limits(limits)
    {
    }
    ~WorldBorder() = default;

    virtual void update(float delta) override
    {
        auto& location = get<Transform>().location;
        location.x = std::clamp(location.x, -m_limits.x, m_limits.x);
        location.y = std::clamp(location.y, -m_limits.y, m_limits.y);
        location.z = std::clamp(location.z, -m_limits.z, m_limits.z);
    }

private:
    glm::vec3 m_limits;
};
}
