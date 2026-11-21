module aegis.world;

import std;
import aegis.reflect;

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
            builder->field("location", &Transform::location, { .step = 0.1, .unit = "m" })
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
        auto builder = registerComponent<PostProcess>(types, "PostProcess");
        if (builder)
        {
            builder->displayName("Post Process")
                .category("Camera")
                .field("toneMapping", &PostProcess::toneMapping, { .tooltip = "How HDR colors map to the screen" })
                .field("exposure", &PostProcess::exposure, { .widget = FieldWidget::Slider, .min = 0.1, .max = 10.0 })
                .field("gamma", &PostProcess::gamma, { .widget = FieldWidget::Slider, .min = 1.0, .max = 5.0, .tooltip = "Not applied to a target that encodes sRGB itself" })
                .field("bloomIntensity", &PostProcess::bloomIntensity, { .widget = FieldWidget::Slider, .min = 0.0, .max = 2.0 })
                .field("bloomThreshold", &PostProcess::bloomThreshold, { .widget = FieldWidget::Slider, .min = 0.0, .max = 2.0, .tooltip = "The brightness bloom starts at" })
                .field("bloomSoftThreshold", &PostProcess::bloomSoftThreshold, { .widget = FieldWidget::Slider, .min = 0.0, .max = 2.0, .tooltip = "How gradually bloom starts (0: a hard cut)" });
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
                .field("intensity", &DirectionalLight::intensity, { .min = 0.0, .step = 0.1 })
                .field("shadowDistance", &DirectionalLight::shadowDistance, { .min = 1.0, .step = 1.0, .tooltip = "Distance in meters from the camera that the shadow covers" })
                .field("shadowBias", &DirectionalLight::shadowBias, { .min = 0.0, .step = 0.1, .tooltip = "Shadow-map texels a surface moves along its normal so it doesn't shadow itself" })
                .field("shadowCascades", &DirectionalLight::shadowCascades, { .widget = FieldWidget::Slider, .min = 1.0, .max = 4.0, .tooltip = "Shadow maps the shadow distance is split into, each sharper nearer the camera" })
                .field("shadowSplitLambda", &DirectionalLight::shadowSplitLambda, { .widget = FieldWidget::Slider, .min = 0.0, .max = 1.0, .tooltip = "How the cascades split the shadow distance: 0 into equal parts, 1 each the same factor longer than the one before" });
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
                .field("intensity", &PointLight::intensity, { .min = 0.0, .step = 1.0 })
                .field("range", &PointLight::range, { .min = 0.01, .step = 0.1, .tooltip = "Distance in meters where the light fades to zero" });
        }
        check(builder);
    }
    {
        auto builder = registerComponent<SpotLight>(types, "SpotLight");
        if (builder)
        {
            builder->displayName("Spot Light")
                .category("Lighting")
                .field("color", &SpotLight::color, { .widget = FieldWidget::Color })
                .field("intensity", &SpotLight::intensity, { .min = 0.0, .step = 1.0 })
                .field("range", &SpotLight::range, { .min = 0.01, .step = 0.1, .tooltip = "Distance in meters where the light fades to zero" })
                .field("innerConeAngle", &SpotLight::innerConeAngle, { .widget = FieldWidget::Angle, .min = 0.0, .max = glm::radians(90.0), .tooltip = "Angle from the axis where the light starts to fade" })
                .field("outerConeAngle", &SpotLight::outerConeAngle, { .widget = FieldWidget::Angle, .min = 0.0, .max = glm::radians(90.0), .tooltip = "Angle from the axis where the light reaches zero" });
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
