module aegis.world.types;

import std;
import aegis.reflect;
import aegis.world.registry;
import aegis.world.render_components;

namespace aegis::world
{
auto registerComponents(reflect::TypeRegistry& types) -> std::expected<void, reflect::Error>
{
    using reflect::FieldWidget;

    // The first failure: a later registration is skipped once one fails
    std::expected<void, reflect::Error> status;
    auto check = [&](const auto& builder) {
        if (!status)
            return;
        if (!builder)
            status = std::unexpected{ builder.error() };
        else if (!builder->status())
            status = builder->status();
    };

    {
        auto builder = registerComponent<Transform>(types, "Transform");
        if (builder)
        {
            builder->field("location", &Transform::location, { .step = 0.1 })
                .field("rotation", &Transform::rotation)
                .field("scale", &Transform::scale, { .step = 0.01 });
        }
        check(builder);
    }
    {
        auto builder = registerComponent<DynamicTag>(types, "DynamicTag");
        if (builder)
            builder->displayName("Dynamic").category("Transform");
        check(builder);
    }
    {
        auto builder = registerSingleton<MainCamera>(types, "MainCamera");
        if (builder)
            builder->displayName("Main Camera").category("Camera");
        check(builder);
    }
    {
        auto builder = registerComponent<Camera>(types, "Camera");
        if (builder)
        {
            builder->category("Camera")
                .field("fov", &Camera::fov, { .widget = FieldWidget::Angle, .min = glm::radians(1.0), .max = glm::radians(179.0), .tooltip = "Vertical field of view" })
                .field("near", &Camera::near, { .min = 0.001, .step = 0.01, .tooltip = "Distance to the near clipping plane" })
                .field("far", &Camera::far, { .min = 0.001, .step = 1.0, .tooltip = "Distance to the far clipping plane" });
        }
        check(builder);
    }
    {
        auto builder = registerComponent<AmbientLight>(types, "AmbientLight");
        if (builder)
        {
            builder->displayName("Ambient Light")
                .category("Lighting")
                .field("color", &AmbientLight::color, { .widget = FieldWidget::Color })
                .field("intensity", &AmbientLight::intensity, { .widget = FieldWidget::Slider, .min = 0.0, .max = 1.0 });
        }
        check(builder);
    }
    {
        auto builder = registerComponent<DirectionalLight>(types, "DirectionalLight");
        if (builder)
        {
            builder->displayName("Directional Light")
                .category("Lighting")
                .field("color", &DirectionalLight::color, { .widget = FieldWidget::Color })
                .field("intensity", &DirectionalLight::intensity, { .min = 0.0, .step = 0.1 });
        }
        check(builder);
    }
    {
        auto builder = registerComponent<PointLight>(types, "PointLight");
        if (builder)
        {
            builder->displayName("Point Light")
                .category("Lighting")
                .field("color", &PointLight::color, { .widget = FieldWidget::Color })
                .field("intensity", &PointLight::intensity, { .min = 0.0, .step = 1.0 });
        }
        check(builder);
    }
    {
        auto builder = registerComponent<MeshRenderer>(types, "MeshRenderer");
        if (builder)
        {
            builder->displayName("Mesh Renderer")
                .category("Rendering")
                .field("mesh", &MeshRenderer::mesh)
                .field("material", &MeshRenderer::material);
        }
        check(builder);
    }
    {
        auto builder = registerSingleton<Sky>(types, "Sky");
        if (builder)
            builder->category("Rendering").field("environment", &Sky::environment);
        check(builder);
    }
    return status;
}
}
