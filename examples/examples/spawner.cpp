import std;
import aegis.engine;

namespace
{
/// @brief The first entity with a MeshRenderer in the subtree of 'entity' (depth first), or a null entity
auto findMesh(aegis::world::Registry& registry, aegis::world::Entity entity) -> aegis::world::Entity
{
    if (registry.has<aegis::MeshRenderer>(entity))
        return entity;
    if (!registry.has<aegis::Children>(entity))
        return {};
    for (auto child = registry.get<aegis::Children>(entity).first; child; child = registry.get<aegis::Siblings>(child).next)
    {
        if (auto found = findMesh(registry, child))
            return found;
    }
    return {};
}

/// @brief Fills a box with clones of the meshes of its children (the templates), for the stress scenes
///
/// The clones take turns over the templates, so differently colored templates give differently colored
/// clones. Their places, sizes and rotations are random but the same every run (the seed).
class Spawner : public aegis::scripting::Script
{
public:
    std::uint32_t count{ 100 };
    glm::vec3 extent{ 10.0f }; // Half the size of the box, around the spawner
    glm::vec2 scale{ 1.0f };   // Range of the clones' uniform scale
    glm::vec3 tilt{ 0.0f };    // Rotation every clone starts with, Euler angles in degrees
    bool tumble{ false };      // Random rotation about every axis, else only about the up axis
    glm::vec2 spin{ 0.0f };    // Range of the clones' spin about the up axis in degrees per second; 0 keeps them static
    std::uint32_t seed{ 1 };

    void onCreate(aegis::scripting::ScriptContext& context) override
    {
        auto& registry = context.registry();
        std::vector<aegis::world::Entity> templates;
        if (registry.has<aegis::Children>(context.entity()))
        {
            for (auto child = registry.get<aegis::Children>(context.entity()).first; child; child = registry.get<aegis::Siblings>(child).next)
            {
                if (auto mesh = findMesh(registry, child))
                    templates.push_back(mesh);
            }
        }
        if (templates.empty())
        {
            aegis::log::warn("Spawner on '{}' has no child with a mesh to clone", context.get<aegis::Name>().name);
            return;
        }

        std::mt19937 rng{ seed };
        auto uniform = [&rng](float min, float max) { return std::uniform_real_distribution<float>{ min, max }(rng); };
        const auto center = context.get<aegis::Transform>().location;
        const bool spinning = spin.y > 0.0f;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const glm::vec3 location{ uniform(-extent.x, extent.x), uniform(-extent.y, extent.y), uniform(-extent.z, extent.z) };
            const glm::vec3 angles = tumble ? glm::vec3{ uniform(0.0f, 360.0f), uniform(0.0f, 360.0f), uniform(0.0f, 360.0f) }
                                            : glm::vec3{ 0.0f, 0.0f, uniform(0.0f, 360.0f) };
            const auto rotation = glm::quat{ glm::radians(tilt + angles) };
            const glm::vec3 size{ uniform(scale.x, scale.y) };

            // Copied, not referenced: adding to the registry may move the template's renderer
            const auto renderer = registry.get<aegis::MeshRenderer>(templates[i % templates.size()]);
            auto clone = registry.create(std::format("Clone {}", i), center + location, rotation, size);
            registry.add<aegis::MeshRenderer>(clone, renderer.mesh, renderer.material);
            if (spinning)
            {
                registry.add<aegis::DynamicTag>(clone); // Moves every frame
                m_spinning.push_back({ clone, glm::radians(uniform(spin.x, spin.y)) });
            }
        }
    }

    void onUpdate(aegis::scripting::ScriptContext& context, float deltaSeconds) override
    {
        auto& registry = context.registry();
        for (const auto& [entity, speed] : m_spinning)
        {
            auto& transform = registry.get<aegis::Transform>(entity);
            transform.rotation *= glm::angleAxis(speed * deltaSeconds, aegis::math::world::UP);
        }
    }

private:
    struct Spinning
    {
        aegis::world::Entity entity;
        float speed; // Radians per second
    };
    std::vector<Spinning> m_spinning;
};
}

namespace examples
{
auto registerSpawner(aegis::reflect::TypeRegistry& types) -> std::expected<void, aegis::reflect::Error>
{
    return aegis::scripting::registerScript<Spawner>(types, "Spawner").and_then([](auto builder) {
        return builder.field("count", &Spawner::count)
            .field("extent", &Spawner::extent)
            .field("scale", &Spawner::scale)
            .field("tilt", &Spawner::tilt)
            .field("tumble", &Spawner::tumble)
            .field("spin", &Spawner::spin)
            .field("seed", &Spawner::seed)
            .status();
    });
}
}
