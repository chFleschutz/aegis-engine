#include <doctest/doctest.h>

#include <cstddef>

import std;
import aegis.render_graph;
import aegis.rhi;
import aegis.math;

// The expected numbers are the byte layout of the Slang twin of each struct (scalar layout: every member
// aligned to its component type, a float3 takes 12 bytes, a pointer or DeviceAddress 8 aligned to 8). They
// are written out by hand, so a reordered or retyped member on either side fails here instead of rendering
// wrong data without a validation error. The Slang structs say which C++ struct mirrors them.

namespace
{
using namespace aegis::render_graph;

// glm's operator== is not part of aegis.math's exports.
auto equal(const glm::vec3& v, float x, float y, float z) -> bool
{
    return v.x == x && v.y == y && v.z == z;
}

auto equal(const glm::vec4& v, float x, float y, float z, float w) -> bool
{
    return v.x == x && v.y == y && v.z == z && v.w == w;
}
}

TEST_SUITE("render_graph::gpu_types")
{
    TEST_CASE("CameraData matches common::Camera")
    {
        // Three row-major 4x4 matrices, the six frustum planes, then a float3 position (no padding).
        CHECK(offsetof(CameraData, view) == 0);
        CHECK(offsetof(CameraData, projection) == 64);
        CHECK(offsetof(CameraData, viewProjection) == 128);
        CHECK(offsetof(CameraData, frustum) == 192);
        CHECK(offsetof(CameraData, position) == 288);
        CHECK(sizeof(CameraData) == 300);
        CHECK(sizeof(Frustum) == 96);
    }

    TEST_CASE("InstanceData matches indirectDraw::Instance with a 112 byte stride")
    {
        CHECK(offsetof(InstanceData, model) == 0);
        CHECK(offsetof(InstanceData, normalRow0) == 48);
        CHECK(offsetof(InstanceData, normalRow1) == 60);
        CHECK(offsetof(InstanceData, normalRow2) == 72);
        CHECK(offsetof(InstanceData, mesh) == 88); // 84 rounded up to the pointer's alignment
        CHECK(offsetof(InstanceData, material) == 96);
        CHECK(offsetof(InstanceData, drawBatch) == 104);
        CHECK(sizeof(InstanceData) == 112);
    }

    TEST_CASE("DrawBatchData matches culling.slang's DrawBatch")
    {
        CHECK(offsetof(DrawBatchData, offset) == 0);
        CHECK(offsetof(DrawBatchData, count) == 4);
        CHECK(sizeof(DrawBatchData) == 8);
    }

    TEST_CASE("LightingUniforms matches lighting.slang's Lighting")
    {
        CHECK(offsetof(LightingUniforms, cameraPosition) == 0);
        CHECK(offsetof(LightingUniforms, ambientColor) == 16);
        CHECK(offsetof(LightingUniforms, directionalDirection) == 32);
        CHECK(offsetof(LightingUniforms, directionalColor) == 48);
        CHECK(offsetof(LightingUniforms, pointLights) == 64);
        CHECK(sizeof(LightingUniforms::PointLight) == 32);
        CHECK(offsetof(LightingUniforms, pointLightCount) == 64 + 128 * 32);
        CHECK(offsetof(LightingUniforms, ambientOcclusionFactor) == 64 + 128 * 32 + 4);
        CHECK(offsetof(LightingUniforms, viewMode) == 64 + 128 * 32 + 8);
        CHECK(sizeof(LightingUniforms) == 64 + 128 * 32 + 12);
        CHECK(maxPointLights == 128);
    }

    TEST_CASE("the enums have the values the shaders switch on")
    {
        CHECK(static_cast<int>(LightingViewMode::SceneColor) == 0);
        CHECK(static_cast<int>(LightingViewMode::Albedo) == 1);
        CHECK(static_cast<int>(LightingViewMode::AmbientOcclusion) == 2);
        CHECK(static_cast<int>(LightingViewMode::Roughness) == 3);
        CHECK(static_cast<int>(LightingViewMode::Metallic) == 4);
        CHECK(static_cast<int>(LightingViewMode::Emissive) == 5);
        CHECK(static_cast<int>(ToneMapping::Reinhard) == 0);
        CHECK(static_cast<int>(ToneMapping::Aces) == 1);
        CHECK(sizeof(LightingViewMode) == 4);
        CHECK(sizeof(ToneMapping) == 4);
    }

    TEST_CASE("CullingConstants matches culling.slang")
    {
        CHECK(offsetof(CullingConstants, camera) == 0);
        CHECK(offsetof(CullingConstants, staticInstances) == 8);
        CHECK(offsetof(CullingConstants, dynamicInstances) == 16);
        CHECK(offsetof(CullingConstants, drawBatches) == 24);
        CHECK(offsetof(CullingConstants, visibility) == 32);
        CHECK(offsetof(CullingConstants, commands) == 40);
        CHECK(offsetof(CullingConstants, counts) == 48);
        CHECK(offsetof(CullingConstants, staticCount) == 56);
        CHECK(offsetof(CullingConstants, dynamicCount) == 60);
        CHECK(sizeof(CullingConstants) == 64);
    }

    TEST_CASE("GpuDrivenGeometryConstants matches indirectDraw::GeometryConstants")
    {
        CHECK(offsetof(GpuDrivenGeometryConstants, camera) == 0);
        CHECK(offsetof(GpuDrivenGeometryConstants, staticInstances) == 8);
        CHECK(offsetof(GpuDrivenGeometryConstants, dynamicInstances) == 16);
        CHECK(offsetof(GpuDrivenGeometryConstants, visibility) == 24);
        CHECK(offsetof(GpuDrivenGeometryConstants, batchFirst) == 32);
        CHECK(offsetof(GpuDrivenGeometryConstants, batchSize) == 36);
        CHECK(offsetof(GpuDrivenGeometryConstants, staticCount) == 40);
        CHECK(offsetof(GpuDrivenGeometryConstants, dynamicCount) == 44);
        CHECK(sizeof(GpuDrivenGeometryConstants) == 48);
    }

    TEST_CASE("StaticMeshConstants matches cpu-driven/geometry.slang")
    {
        CHECK(offsetof(StaticMeshConstants, model) == 0);
        CHECK(offsetof(StaticMeshConstants, normalRow0) == 48);
        CHECK(offsetof(StaticMeshConstants, normalRow1) == 60);
        CHECK(offsetof(StaticMeshConstants, normalRow2) == 72);
        CHECK(offsetof(StaticMeshConstants, camera) == 88);
        CHECK(offsetof(StaticMeshConstants, mesh) == 96);
        CHECK(offsetof(StaticMeshConstants, material) == 104);
        CHECK(sizeof(StaticMeshConstants) == 112);
    }

    TEST_CASE("LightingConstants matches lighting.slang")
    {
        CHECK(offsetof(LightingConstants, lighting) == 0);
        CHECK(offsetof(LightingConstants, sceneColor) == 8);
        CHECK(offsetof(LightingConstants, gBuffer) == 12); // Position, normal, albedo, ARM, emissive
        CHECK(offsetof(LightingConstants, irradiance) == 32);
        CHECK(offsetof(LightingConstants, prefiltered) == 40);
        CHECK(offsetof(LightingConstants, brdfLut) == 48);
        CHECK(sizeof(LightingConstants) == 56);
    }

    TEST_CASE("SkyBoxConstants matches sky_box.slang")
    {
        CHECK(offsetof(SkyBoxConstants, viewProjection) == 0);
        CHECK(offsetof(SkyBoxConstants, skybox) == 64);
        CHECK(sizeof(SkyBoxConstants) == 72);
    }

    TEST_CASE("PostProcessConstants matches post_process.slang")
    {
        CHECK(offsetof(PostProcessConstants, final) == 0);
        CHECK(offsetof(PostProcessConstants, sceneColor) == 4);
        CHECK(offsetof(PostProcessConstants, bloom) == 8);
        CHECK(offsetof(PostProcessConstants, toneMapping) == 12);
        CHECK(offsetof(PostProcessConstants, bloomIntensity) == 16);
        CHECK(offsetof(PostProcessConstants, exposure) == 20);
        CHECK(offsetof(PostProcessConstants, gamma) == 24);
        CHECK(sizeof(PostProcessConstants) == 28);
    }

    TEST_CASE("PointLightConstants matches point_light.slang")
    {
        CHECK(offsetof(PointLightConstants, worldPosition) == 0);
        CHECK(offsetof(PointLightConstants, color) == 16);
        CHECK(offsetof(PointLightConstants, radius) == 32);
        CHECK(offsetof(PointLightConstants, camera) == 40); // 36 rounded up to the pointer's alignment
        CHECK(sizeof(PointLightConstants) == 48);
    }

    TEST_CASE("the bloom constants match bloom/*.slang")
    {
        CHECK(offsetof(BloomThresholdConstants, input) == 0);
        CHECK(offsetof(BloomThresholdConstants, output) == 4);
        CHECK(offsetof(BloomThresholdConstants, threshold) == 8);
        CHECK(offsetof(BloomThresholdConstants, softThreshold) == 12);
        CHECK(sizeof(BloomThresholdConstants) == 16);

        CHECK(offsetof(BloomDownsampleConstants, output) == 0);
        CHECK(offsetof(BloomDownsampleConstants, input) == 4);
        CHECK(offsetof(BloomDownsampleConstants, mipLevel) == 12);
        CHECK(offsetof(BloomDownsampleConstants, filterScale) == 16);
        CHECK(sizeof(BloomDownsampleConstants) == 20);

        CHECK(offsetof(BloomUpsampleConstants, output) == 0);
        CHECK(offsetof(BloomUpsampleConstants, input) == 4);
        CHECK(offsetof(BloomUpsampleConstants, filterScale) == 12);
        CHECK(sizeof(BloomUpsampleConstants) == 16);
    }

    TEST_CASE("the IBL constants match ibl/*.slang")
    {
        CHECK(offsetof(EquirectToCubeConstants, equirect) == 0);
        CHECK(offsetof(EquirectToCubeConstants, cube) == 8);
        CHECK(sizeof(EquirectToCubeConstants) == 12);

        CHECK(offsetof(IrradianceConstants, environment) == 0);
        CHECK(offsetof(IrradianceConstants, irradiance) == 8);
        CHECK(sizeof(IrradianceConstants) == 12);

        CHECK(offsetof(PrefilterConstants, environment) == 0);
        CHECK(offsetof(PrefilterConstants, prefiltered) == 8);
        CHECK(offsetof(PrefilterConstants, roughness) == 12);
        CHECK(offsetof(PrefilterConstants, envResolution) == 16);
        CHECK(sizeof(PrefilterConstants) == 20);

        CHECK(sizeof(BrdfLutConstants) == 4);
    }

    TEST_CASE("every push constant block fits the 128 bytes of the shared pipeline layout")
    {
        constexpr auto limit = aegis::rhi::CommandBuffer::maxPushConstantSize;
        CHECK(sizeof(CullingConstants) <= limit);
        CHECK(sizeof(GpuDrivenGeometryConstants) <= limit);
        CHECK(sizeof(StaticMeshConstants) <= limit);
        CHECK(sizeof(LightingConstants) <= limit);
        CHECK(sizeof(SkyBoxConstants) <= limit);
        CHECK(sizeof(PostProcessConstants) <= limit);
        CHECK(sizeof(PointLightConstants) <= limit);
        CHECK(sizeof(BloomThresholdConstants) <= limit);
        CHECK(sizeof(BloomDownsampleConstants) <= limit);
        CHECK(sizeof(BloomUpsampleConstants) <= limit);
        CHECK(sizeof(EquirectToCubeConstants) <= limit);
        CHECK(sizeof(IrradianceConstants) <= limit);
        CHECK(sizeof(PrefilterConstants) <= limit);
        CHECK(sizeof(BrdfLutConstants) <= limit);
    }
}

TEST_SUITE("render_graph::detail::toGpuTransform")
{
    using aegis::render_graph::detail::toGpuTransform;

    TEST_CASE("an identity matrix gives identity rows")
    {
        const auto transform = toGpuTransform(glm::mat4{ 1.0f });
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 4; ++column)
                CHECK(transform.model[row][column] == (row == column ? 1.0f : 0.0f));
        }
        CHECK(equal(transform.normalRow0, 1.0f, 0.0f, 0.0f));
        CHECK(equal(transform.normalRow1, 0.0f, 1.0f, 0.0f));
        CHECK(equal(transform.normalRow2, 0.0f, 0.0f, 1.0f));
    }

    TEST_CASE("the model rows hold the translation in w, and the normal rows are the inverse transpose")
    {
        // Scale (2, 4, 8) after translation (10, 20, 30): glm builds matrices column by column.
        glm::mat4 model{ 1.0f };
        model[0][0] = 2.0f;
        model[1][1] = 4.0f;
        model[2][2] = 8.0f;
        model[3] = glm::vec4{ 10.0f, 20.0f, 30.0f, 1.0f };

        const auto transform = toGpuTransform(model);
        CHECK(equal(transform.model[0], 2.0f, 0.0f, 0.0f, 10.0f));
        CHECK(equal(transform.model[1], 0.0f, 4.0f, 0.0f, 20.0f));
        CHECK(equal(transform.model[2], 0.0f, 0.0f, 8.0f, 30.0f));
        CHECK(equal(transform.normalRow0, 0.5f, 0.0f, 0.0f));
        CHECK(equal(transform.normalRow1, 0.0f, 0.25f, 0.0f));
        CHECK(equal(transform.normalRow2, 0.0f, 0.0f, 0.125f));
    }

    TEST_CASE("a shear shows which way the normal rows are stored")
    {
        // x' = x + 2y (a shear along x). The inverse is x = x' - 2y', so the inverse's column 1 is
        // (-2, 1, 0); as a row of the inverse transpose it is row 1.
        glm::mat4 model{ 1.0f };
        model[1][0] = 2.0f; // Column 1, row 0
        const auto transform = toGpuTransform(model);
        CHECK(equal(transform.model[0], 1.0f, 2.0f, 0.0f, 0.0f));
        CHECK(equal(transform.normalRow0, 1.0f, 0.0f, 0.0f));
        CHECK(equal(transform.normalRow1, -2.0f, 1.0f, 0.0f));
        CHECK(equal(transform.normalRow2, 0.0f, 0.0f, 1.0f));
    }
}
