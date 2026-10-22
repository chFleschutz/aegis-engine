module;
#include "aegis/base/flags.h"

export module aegis.render_passes:gpu_types;
import std;
import aegis.render_graph;
import aegis.rhi;
import aegis.math;

// The C++ side of the CPU/GPU contract of the render graph's shaders: every struct a shader reads
// through a push constant or a buffer address, and the values both sides must agree on. Each struct is
// named after its Slang twin and mirrors it member for member in scalar layout (shaders/modules/README.md);
// tests/render_graph/gpu_types.test.cpp pins sizes and offsets. Change a struct here and its Slang twin
// together.
//
// Mirrors of the asset types (assets::Vertex, assets::Meshlet, ...) live in aegis.assets:gpu_types.

export namespace aegis::render_graph
{
/// @brief Threads per workgroup side of the 2D compute passes: [numthreads(16, 16, 1)] in lighting.slang,
///        post_process.slang, bloom/*.slang and ibl/*.slang.
inline constexpr std::uint32_t computeGroupSize{ 16 };
/// @brief Threads per workgroup of the culling shader: [numthreads(64, 1, 1)] in gpu-driven/culling.slang.
inline constexpr std::uint32_t cullingGroupSize{ 64 };
/// @brief lighting.slang's MAX_POINT_LIGHTS.
inline constexpr std::uint32_t maxPointLights{ 128 };
/// @brief lighting.slang's MAX_REFLECTION_LOD: the last mip of the prefiltered environment map (checked
///        against Ibl::prefilteredMipLevels).
inline constexpr float maxReflectionLod{ 4.0f };

/// @brief lighting.slang's view modes (the 'viewMode' switch).
enum class LightingViewMode : std::int32_t
{
    SceneColor = 0,
    Albedo = 1,
    AmbientOcclusion = 2,
    Roughness = 3,
    Metallic = 4,
    Emissive = 5,
};

/// @brief post_process.slang's tone mapping modes (the 'toneMappingMode' switch).
enum class ToneMapping : std::int32_t
{
    Reinhard = 0,
    Aces = 1,
};

/// @brief indirect_draw.slang's INSTANCE_* bits: InstanceData::flags.
enum class InstanceFlags : std::uint32_t
{
    None = 0,
    Mirrored = 1 << 0, // The model matrix has a negative determinant: its triangles wind the other way
};
AEGIS_FLAGS(InstanceFlags)

// Buffers ---------------------------------------------------------------------------------------------

/// @brief common::Camera: the main camera, written by the scene update pass once per frame. Matrices are
///        row-major.
struct CameraData
{
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 viewProjection;
    Frustum frustum;
    glm::vec3 position;
};

/// @brief indirectDraw::Instance: one drawable instance as the GPU-driven shaders read it.
struct InstanceData
{
    glm::mat3x4 model; // Top three rows of the model matrix
    glm::vec3 normalRow0;
    glm::vec3 normalRow1;
    glm::vec3 normalRow2;
    rhi::DeviceAddress mesh;     // assets::MeshGpuData
    rhi::DeviceAddress material; // assets::Material parameter block (common::PbrMaterial)
    std::uint32_t drawBatch;
    InstanceFlags flags;
};

/// @brief culling.slang's DrawBatch: where a draw batch's instances live in the visibility and indirect
///        command buffers.
struct DrawBatchData
{
    std::uint32_t offset;
    std::uint32_t count;
};

/// @brief lighting.slang's Lighting block, one per frame in flight.
struct LightingUniforms
{
    struct PointLight
    {
        glm::vec4 position;
        glm::vec4 color; // rgb, intensity
    };

    glm::vec4 cameraPosition{ 0.0f };
    glm::vec4 ambientColor{ 0.0f }; // rgb, intensity
    glm::vec4 directionalDirection{ 0.0f, 0.0f, 1.0f, 0.0f };
    glm::vec4 directionalColor{ 0.0f }; // rgb, intensity
    std::array<PointLight, maxPointLights> pointLights{};
    std::int32_t pointLightCount{ 0 };
    float ambientOcclusionFactor{ 1.0f };
    LightingViewMode viewMode{ LightingViewMode::SceneColor };
};

// Push constants --------------------------------------------------------------------------------------
// Each is the PushConstants struct of the shader named in its comment. At most
// rhi::CommandBuffer::maxPushConstantSize bytes, which CommandBuffer::pushConstants checks.

/// @brief gpu-driven/culling.slang
struct CullingConstants
{
    rhi::DeviceAddress camera;
    rhi::DeviceAddress staticInstances;
    rhi::DeviceAddress dynamicInstances;
    rhi::DeviceAddress drawBatches;
    rhi::DeviceAddress visibility;
    rhi::DeviceAddress commands;
    rhi::DeviceAddress counts;
    std::uint32_t staticCount;
    std::uint32_t dynamicCount;
};

/// @brief indirectDraw::GeometryConstants (gpu-driven/task.slang and geometry.slang)
struct GpuDrivenGeometryConstants
{
    rhi::DeviceAddress camera;
    rhi::DeviceAddress staticInstances;
    rhi::DeviceAddress dynamicInstances;
    rhi::DeviceAddress visibility; // Visible instance IDs, grouped by draw batch
    std::uint32_t batchFirst;
    std::uint32_t batchSize;
    std::uint32_t staticCount;
    std::uint32_t dynamicCount;
};

/// @brief cpu-driven/geometry.slang
struct StaticMeshConstants
{
    glm::mat3x4 model; // Top three rows
    glm::vec3 normalRow0;
    glm::vec3 normalRow1;
    glm::vec3 normalRow2;
    rhi::DeviceAddress camera;
    rhi::DeviceAddress mesh;
    rhi::DeviceAddress material;
};

/// @brief lighting.slang
struct LightingConstants
{
    rhi::DeviceAddress lighting; // LightingUniforms
    rhi::StorageImageDescriptor sceneColor;
    std::array<rhi::StorageImageDescriptor, 5> gBuffer; // Position, Normal, Albedo, ARM, Emissive
    rhi::TextureDescriptor irradiance;
    rhi::TextureDescriptor prefiltered;
    rhi::TextureDescriptor brdfLut;
};

/// @brief sky_box.slang
struct SkyBoxConstants
{
    glm::mat4 viewProjection;
    rhi::TextureDescriptor skybox;
};

/// @brief post_process.slang
struct PostProcessConstants
{
    rhi::StorageImageDescriptor final;
    rhi::StorageImageDescriptor sceneColor;
    rhi::StorageImageDescriptor bloom;
    ToneMapping toneMapping;
    float bloomIntensity;
    float exposure;
    float gamma;
};

/// @brief point_light.slang
struct PointLightConstants
{
    glm::vec4 worldPosition;
    glm::vec4 color;
    float radius;
    rhi::DeviceAddress camera;
};

/// @brief bloom/threshold.slang
struct BloomThresholdConstants
{
    rhi::StorageImageDescriptor input;
    rhi::StorageImageDescriptor output;
    float threshold;
    float softThreshold;
};

/// @brief bloom/downsample.slang
struct BloomDownsampleConstants
{
    rhi::StorageImageDescriptor output;
    rhi::TextureDescriptor input;
    std::int32_t mipLevel;
    float filterScale;
};

/// @brief bloom/upsample.slang
struct BloomUpsampleConstants
{
    rhi::StorageImageDescriptor output;
    rhi::TextureDescriptor input;
    float filterScale;
};

/// @brief ibl/equirect_to_cube.slang
struct EquirectToCubeConstants
{
    rhi::TextureDescriptor equirect;
    rhi::StorageImageDescriptor cube;
};

/// @brief ibl/irradiance_convolution.slang
struct IrradianceConstants
{
    rhi::TextureDescriptor environment;
    rhi::StorageImageDescriptor irradiance;
};

/// @brief ibl/prefilter_environment.slang
struct PrefilterConstants
{
    rhi::TextureDescriptor environment;
    rhi::StorageImageDescriptor prefiltered;
    float roughness;
    float envResolution;
};

/// @brief ibl/brdf_lut.slang
struct BrdfLutConstants
{
    rhi::StorageImageDescriptor lut;
};
}

export namespace aegis::render_graph::detail
{
/// @brief A model matrix as shaders read it: its top three rows, the rows of its normal matrix, and
///        whether it mirrors. Copy the members into InstanceData or StaticMeshConstants; the CPU-driven
///        path has no flags in its constants and picks a pipeline with the other front face instead.
struct GpuTransform
{
    glm::mat3x4 model; // Row r of the model matrix is model[r], translation in .w
    glm::vec3 normalRow0;
    glm::vec3 normalRow1;
    glm::vec3 normalRow2;
    InstanceFlags flags{ InstanceFlags::None };
};

/// @brief The GpuTransform of a model matrix given by its top three rows (toModelRows). The normal matrix is
///        the inverse transpose of the upper 3x3; its rows are the columns of the inverse, which is how glm
///        stores them. A negative determinant (a mirror, e.g. a scale of -1 on one axis) sets
///        InstanceFlags::Mirrored.
[[nodiscard]] inline auto toGpuTransform(const glm::mat3x4& rows) -> GpuTransform
{
    glm::mat3 linear; // Column-major: column c, row r is rows[r][c]
    for (int column = 0; column < 3; ++column)
    {
        for (int row = 0; row < 3; ++row)
            linear[column][row] = rows[row][column];
    }
    const auto normalMatrix = glm::inverse(linear);
    return {
        .model = rows,
        .normalRow0 = normalMatrix[0],
        .normalRow1 = normalMatrix[1],
        .normalRow2 = normalMatrix[2],
        .flags = glm::determinant(linear) < 0.0f ? InstanceFlags::Mirrored : InstanceFlags::None,
    };
}

/// @brief The row-major form of 'model' for shaders (see the overload taking its rows).
[[nodiscard]] inline auto toGpuTransform(const glm::mat4& model) -> GpuTransform
{
    return toGpuTransform(toModelRows(model));
}
}
