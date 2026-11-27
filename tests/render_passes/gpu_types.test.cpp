#include <doctest/doctest.h>

#include <cstddef>

import std;
import aegis.assets;
import aegis.render_passes;
import aegis.rhi;
import aegis.math;

namespace math = aegis::math;

// The expected numbers are the byte layout of the Slang twin of each struct (scalar layout: every member
// aligned to its component type, a float3 takes 12 bytes, a pointer or DeviceAddress 8 aligned to 8). They
// are written out by hand, so a reordered or retyped member on either side fails here instead of rendering
// wrong data without a validation error. The Slang structs say which C++ struct mirrors them.

namespace
{
using namespace aegis::render_graph;

auto equal(const math::Float3& v, float x, float y, float z) -> bool
{
    return v.x == x && v.y == y && v.z == z;
}

auto equal(const math::Float4& v, float x, float y, float z, float w) -> bool
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
        CHECK(offsetof(InstanceData, batchSlot) == 84); // Fills the gap before the pointer's alignment
        CHECK(offsetof(InstanceData, mesh) == 88);
        CHECK(offsetof(InstanceData, material) == 96);
        CHECK(offsetof(InstanceData, drawBatch) == 104);
        CHECK(offsetof(InstanceData, flags) == 108);
        CHECK(sizeof(InstanceData) == 112);
    }

    TEST_CASE("DrawBatchData matches indirectDraw::DrawBatch")
    {
        CHECK(offsetof(DrawBatchData, offset) == 0);
        CHECK(offsetof(DrawBatchData, count) == 4);
        CHECK(sizeof(DrawBatchData) == 8);
    }

    TEST_CASE("LightingUniforms matches lighting::Lighting")
    {
        CHECK(offsetof(LightingUniforms, cameraPosition) == 0);
        CHECK(offsetof(LightingUniforms, ambientColor) == 16);
        CHECK(offsetof(LightingUniforms, directionalDirection) == 32);
        CHECK(offsetof(LightingUniforms, directionalColor) == 48);
        CHECK(offsetof(LightingUniforms, lights) == 64);
        CHECK(offsetof(LightingUniforms, pointLightCount) == 72);
        CHECK(offsetof(LightingUniforms, spotLightCount) == 76);
        CHECK(offsetof(LightingUniforms, ambientOcclusionFactor) == 80);
        CHECK(offsetof(LightingUniforms, viewMode) == 84);
        CHECK(offsetof(LightingUniforms, cameraForward) == 88);
        CHECK(offsetof(LightingUniforms, shadowCascades) == 104);
        CHECK(offsetof(LightingUniforms, shadowBias) == 424);
        CHECK(offsetof(LightingUniforms, shadowCascadeBlend) == 428);
        CHECK(offsetof(LightingUniforms, shadowCascadeCount) == 432);
        CHECK(offsetof(LightingUniforms, spotShadowAtlas) == 436);
        CHECK(offsetof(LightingUniforms, spotShadows) == 444);
        CHECK(offsetof(LightingUniforms, irradiance) == 448); // lighting::Environment
        CHECK(offsetof(LightingUniforms, prefiltered) == 456);
        CHECK(offsetof(LightingUniforms, brdfLut) == 464);
        CHECK(sizeof(LightingUniforms) == 472);
    }

    TEST_CASE("ShadowCascadeData matches lighting::ShadowCascade")
    {
        CHECK(offsetof(ShadowCascadeData, viewProjection) == 0);
        CHECK(offsetof(ShadowCascadeData, map) == 64);
        CHECK(offsetof(ShadowCascadeData, texelSize) == 72);
        CHECK(offsetof(ShadowCascadeData, splitFar) == 76);
        CHECK(sizeof(ShadowCascadeData) == 80);
    }

    TEST_CASE("LightData matches lighting::Light")
    {
        CHECK(offsetof(LightData, position) == 0);
        CHECK(offsetof(LightData, range) == 12);
        CHECK(offsetof(LightData, color) == 16);
        CHECK(offsetof(LightData, intensity) == 28);
        CHECK(offsetof(LightData, direction) == 32);
        CHECK(offsetof(LightData, coneScale) == 44);
        CHECK(offsetof(LightData, coneOffset) == 48);
        CHECK(offsetof(LightData, shadowBias) == 52);
        CHECK(offsetof(LightData, shadowTexelSize) == 56);
        CHECK(offsetof(LightData, shadowRegion) == 60);
        CHECK(offsetof(LightData, shadowViewProjection) == 76);
        CHECK(sizeof(LightData) == 140);
    }

    TEST_CASE("the enums have the values the shaders switch on")
    {
        CHECK(static_cast<int>(LightingViewMode::SceneColor) == 0);
        CHECK(static_cast<int>(LightingViewMode::Albedo) == 1);
        CHECK(static_cast<int>(LightingViewMode::AmbientOcclusion) == 2);
        CHECK(static_cast<int>(LightingViewMode::Roughness) == 3);
        CHECK(static_cast<int>(LightingViewMode::Metallic) == 4);
        CHECK(static_cast<int>(LightingViewMode::Emissive) == 5);
        CHECK(static_cast<int>(LightingViewMode::ShadowCascades) == 6);
        CHECK(static_cast<int>(ToneMapping::Reinhard) == 0);
        CHECK(static_cast<int>(ToneMapping::Aces) == 1);
        CHECK(sizeof(LightingViewMode) == 4);
        CHECK(sizeof(ToneMapping) == 4);
        CHECK(static_cast<std::uint32_t>(InstanceFlags::Mirrored) == 1); // indirect_draw.slang's INSTANCE_MIRRORED
        CHECK(sizeof(InstanceFlags) == 4);
    }

    TEST_CASE("CullingConstants matches culling.slang")
    {
        CHECK(offsetof(CullingConstants, camera) == 0);
        CHECK(offsetof(CullingConstants, staticInstances) == 8);
        CHECK(offsetof(CullingConstants, dynamicInstances) == 16);
        CHECK(offsetof(CullingConstants, drawBatches) == 24);
        CHECK(offsetof(CullingConstants, visibility) == 32);
        CHECK(offsetof(CullingConstants, commands) == 40);
        CHECK(offsetof(CullingConstants, staticCount) == 48);
        CHECK(offsetof(CullingConstants, dynamicCount) == 52);
        CHECK(sizeof(CullingConstants) == 56);
    }

    TEST_CASE("CompactionConstants matches compaction.slang")
    {
        CHECK(offsetof(CompactionConstants, drawBatches) == 0);
        CHECK(offsetof(CompactionConstants, visibility) == 8);
        CHECK(offsetof(CompactionConstants, commands) == 16);
        CHECK(offsetof(CompactionConstants, counts) == 24);
        CHECK(sizeof(CompactionConstants) == 32);
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
        CHECK(offsetof(GpuDrivenGeometryConstants, flags) == 48);
        CHECK(sizeof(GpuDrivenGeometryConstants) == 56); // The shader's 52, rounded up to the pointers' alignment
    }

    TEST_CASE("only a batch whose pipeline culls back faces culls its meshlets facing away")
    {
        using aegis::rhi::CullMode;
        CHECK(geometryFlags(PipelineState{ .cullMode = CullMode::Back }) == GeometryFlags::CullBackFacing);
        CHECK(geometryFlags(PipelineState{ .cullMode = CullMode::None }) == GeometryFlags::None);
        CHECK(geometryFlags(PipelineState{ .cullMode = CullMode::Front }) == GeometryFlags::None);
    }

    TEST_CASE("a double-sided variant's G-buffer and depth batches keep their meshlets facing away")
    {
        auto key = PipelineKey{ .variant = aegis::assets::MaterialVariant{ .cullMode = aegis::rhi::CullMode::None }, .path = GeometryPath::GpuDriven };
        CHECK(geometryFlags(pipelineState(key)) == GeometryFlags::None);
        key.technique = Technique::Depth;
        CHECK(geometryFlags(pipelineState(key)) == GeometryFlags::None);
        key.variant.cullMode = aegis::rhi::CullMode::Back;
        CHECK(geometryFlags(pipelineState(key)) == GeometryFlags::CullBackFacing);
    }

    TEST_CASE("StaticMeshConstants matches cpu_driven/geometry.slang")
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

    TEST_CASE("ForwardConstants matches cpu_driven/forward.slang")
    {
        CHECK(offsetof(ForwardConstants, model) == 0);
        CHECK(offsetof(ForwardConstants, normalRow0) == 48);
        CHECK(offsetof(ForwardConstants, normalRow1) == 60);
        CHECK(offsetof(ForwardConstants, normalRow2) == 72);
        CHECK(offsetof(ForwardConstants, camera) == 88);
        CHECK(offsetof(ForwardConstants, mesh) == 96);
        CHECK(offsetof(ForwardConstants, material) == 104);
        CHECK(offsetof(ForwardConstants, lighting) == 112);
        CHECK(sizeof(ForwardConstants) == 120);
    }

    TEST_CASE("LightingConstants matches lighting.slang")
    {
        CHECK(offsetof(LightingConstants, lighting) == 0);
        CHECK(offsetof(LightingConstants, sceneColor) == 8);
        CHECK(offsetof(LightingConstants, gBuffer) == 12); // Position, normal, albedo, ARM, emissive
        CHECK(sizeof(LightingConstants) == 32);
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
        CHECK(sizeof(CompactionConstants) <= limit);
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
        const auto transform = toGpuTransform(math::Float4x4::identity());
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 4; ++column)
                CHECK(transform.model[row, column] == (row == column ? 1.0f : 0.0f));
        }
        CHECK(equal(transform.normalRow0, 1.0f, 0.0f, 0.0f));
        CHECK(equal(transform.normalRow1, 0.0f, 1.0f, 0.0f));
        CHECK(equal(transform.normalRow2, 0.0f, 0.0f, 1.0f));
    }

    TEST_CASE("the model rows hold the translation in w, and the normal rows are the inverse transpose")
    {
        // Scale (2, 4, 8) after translation (10, 20, 30).
        const auto model = math::Float4x4::fromRows(
            math::Float4{ 2.0f, 0.0f, 0.0f, 10.0f },
            math::Float4{ 0.0f, 4.0f, 0.0f, 20.0f },
            math::Float4{ 0.0f, 0.0f, 8.0f, 30.0f },
            math::Float4{ 0.0f, 0.0f, 0.0f, 1.0f });

        const auto transform = toGpuTransform(model);
        CHECK(equal(transform.model.row(0), 2.0f, 0.0f, 0.0f, 10.0f));
        CHECK(equal(transform.model.row(1), 0.0f, 4.0f, 0.0f, 20.0f));
        CHECK(equal(transform.model.row(2), 0.0f, 0.0f, 8.0f, 30.0f));
        CHECK(equal(transform.normalRow0, 0.5f, 0.0f, 0.0f));
        CHECK(equal(transform.normalRow1, 0.0f, 0.25f, 0.0f));
        CHECK(equal(transform.normalRow2, 0.0f, 0.0f, 0.125f));
    }

    TEST_CASE("a shear shows which way the normal rows are stored")
    {
        // x' = x + 2y (a shear along x). The inverse is x = x' - 2y', so the inverse's column 1 is
        // (-2, 1, 0); as a row of the inverse transpose it is row 1.
        math::Float4x4 model = math::Float4x4::identity();
        model[0, 1] = 2.0f;
        const auto transform = toGpuTransform(model);
        CHECK(equal(transform.model.row(0), 1.0f, 2.0f, 0.0f, 0.0f));
        CHECK(equal(transform.normalRow0, 1.0f, 0.0f, 0.0f));
        CHECK(equal(transform.normalRow1, -2.0f, 1.0f, 0.0f));
        CHECK(equal(transform.normalRow2, 0.0f, 0.0f, 1.0f));
    }

    TEST_CASE("a plain transform is not flagged as mirrored")
    {
        // Rotated, scaled and moved, but with a positive determinant.
        const auto model = math::Float4x4::fromColumns(
            math::Float4{ 0.0f, 2.0f, 0.0f, 0.0f },
            math::Float4{ -3.0f, 0.0f, 0.0f, 0.0f },
            math::Float4{ 0.0f, 0.0f, 1.0f, 0.0f },
            math::Float4{ 5.0f, 6.0f, 7.0f, 1.0f });
        CHECK(toGpuTransform(math::Float4x4::identity()).flags == InstanceFlags::None);
        CHECK(toGpuTransform(model).flags == InstanceFlags::None);
    }

    TEST_CASE("a negative scale on one or three axes is flagged as mirrored")
    {
        math::Float4x4 oneAxis = math::Float4x4::identity();
        oneAxis[0, 0] = -1.0f;
        const auto threeAxes = math::Float4x4::fromRows(
            math::Float4{ -2.0f, 0.0f, 0.0f, 1.0f },
            math::Float4{ 0.0f, -1.0f, 0.0f, 2.0f },
            math::Float4{ 0.0f, 0.0f, -0.5f, 3.0f },
            math::Float4{ 0.0f, 0.0f, 0.0f, 1.0f });
        CHECK(toGpuTransform(oneAxis).flags == InstanceFlags::Mirrored);
        CHECK(toGpuTransform(threeAxes).flags == InstanceFlags::Mirrored);
    }

    TEST_CASE("a negative scale on two axes is a rotation, not a mirror")
    {
        math::Float4x4 model = math::Float4x4::identity();
        model[0, 0] = -1.0f;
        model[1, 1] = -1.0f;
        CHECK(toGpuTransform(model).flags == InstanceFlags::None);
    }
}
