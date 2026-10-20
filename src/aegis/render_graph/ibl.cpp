module aegis.render_graph;
import std;
import aegis.assets;
import aegis.rhi;

namespace aegis::render_graph
{
namespace
{
// An assets failure keeps its rhi cause.
auto fromAssets(const assets::Error& error, std::string_view context) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .code = ErrorCode::InitializationFailed,
        .message = std::format("{}: {}", context, error.message),
        .rhi = error.rhi } };
}

auto groups(std::uint32_t size) -> std::uint32_t
{
    return groupCount(size, computeGroupSize);
}

// lighting.slang reads the last mip of the prefiltered map as the roughest reflection.
static_assert(maxReflectionLod == static_cast<float>(Ibl::prefilteredMipLevels - 1));

auto cubeDesc(std::string_view name, std::uint32_t size, std::uint32_t mipLevels) -> assets::Texture::Desc
{
    return assets::Texture::Desc{
        .name = name,
        .type = rhi::ImageType::Cube,
        .extent = { size, size, 1 },
        .format = rhi::Format::RGBA16_SFLOAT,
        .mipLevels = mipLevels,
        .arrayLayers = 6,
        .sampler = { .addressMode = rhi::SamplerAddressMode::ClampToEdge },
        .usage = rhi::ImageUsage::Storage,
    };
}
}

auto Ibl::create(rhi::Device& device) -> std::expected<Ibl, Error>
{
    auto equirectToCube = createComputePipeline(device, "IblEquirectToCube", "ibl/equirect_to_cube.slang.spv");
    if (!equirectToCube)
        return std::unexpected{ equirectToCube.error() };
    auto irradiance = createComputePipeline(device, "IblIrradiance", "ibl/irradiance_convolution.slang.spv");
    if (!irradiance)
        return std::unexpected{ irradiance.error() };
    auto prefilter = createComputePipeline(device, "IblPrefilter", "ibl/prefilter_environment.slang.spv");
    if (!prefilter)
        return std::unexpected{ prefilter.error() };
    auto brdfLut = createComputePipeline(device, "IblBrdfLut", "ibl/brdf_lut.slang.spv");
    if (!brdfLut)
        return std::unexpected{ brdfLut.error() };

    return Ibl{ device, std::move(*equirectToCube), std::move(*irradiance), std::move(*prefilter), std::move(*brdfLut) };
}

Ibl::Ibl(rhi::Device& device, rhi::Pipeline equirectToCube, rhi::Pipeline irradiance, rhi::Pipeline prefilter,
    rhi::Pipeline brdfLut) :
    m_device{ &device },
    m_equirectToCube{ std::move(equirectToCube) },
    m_irradiance{ std::move(irradiance) },
    m_prefilter{ std::move(prefilter) },
    m_brdfLut{ std::move(brdfLut) }
{
}

auto Ibl::loadEnvironment(const std::filesystem::path& hdr) -> std::expected<assets::Environment, Error>
{
    auto equirect = assets::Texture::loadEquirectangular(*m_device, hdr);
    if (!equirect)
        return fromAssets(equirect.error(), "Failed to load environment");

    auto cube = cubeFromEquirectangular(**equirect);
    if (!cube)
        return std::unexpected{ cube.error() };
    return environmentFrom(std::move(*cube));
}

auto Ibl::environmentFrom(std::shared_ptr<assets::Texture> cube) -> std::expected<assets::Environment, Error>
{
    auto irradianceMap = irradiance(*cube);
    if (!irradianceMap)
        return std::unexpected{ irradianceMap.error() };
    auto prefilteredMap = prefiltered(*cube);
    if (!prefilteredMap)
        return std::unexpected{ prefilteredMap.error() };
    auto lut = brdfLut();
    if (!lut)
        return std::unexpected{ lut.error() };

    return assets::Environment{
        .skybox = std::move(cube),
        .irradiance = std::move(*irradianceMap),
        .prefiltered = std::move(*prefilteredMap),
        .brdfLUT = std::move(*lut),
    };
}

auto Ibl::storageView(const assets::Texture& texture, std::uint32_t mipLevel) -> std::expected<rhi::ImageViewHandle, Error>
{
    const auto& image = m_device->get(texture.image());
    const auto extent = image.extent();
    auto view = m_device->createImageView(texture.image(),
        rhi::ImageView::Desc{
            .name = "IblStorageView",
            .type = image.arrayLayers() > 1 ? rhi::ImageViewType::Dim2DArray : rhi::ImageViewType::Dim2D,
            .extent = { std::max(extent.x >> mipLevel, 1u), std::max(extent.y >> mipLevel, 1u), 1 },
            .format = image.format(),
            .range = { .baseMipLevel = mipLevel, .mipLevelCount = 1, .arrayLayerCount = image.arrayLayers() },
            .usage = rhi::ImageUsage::Storage,
        });
    if (!view)
        return makeError(ErrorCode::ResourceCreationFailed, "Failed to create IBL storage view", view.error());
    return *view;
}

auto Ibl::cubeFromEquirectangular(const assets::Texture& equirect) -> std::expected<std::shared_ptr<assets::Texture>, Error>
{
    // Four faces span the equirectangular image's width.
    const auto size = m_device->get(equirect.image()).extent().x / 4;
    auto cube = assets::Texture::create(*m_device, cubeDesc("EnvironmentCube", size, rhi::Image::fullMipChain));
    if (!cube)
        return fromAssets(cube.error(), "Failed to create environment cube");
    auto target = storageView(**cube, 0);
    if (!target)
        return std::unexpected{ target.error() };

    const EquirectToCubeConstants constants{ equirect.descriptor(), *m_device->get(*target).storageDescriptor() };

    const auto image = (*cube)->image();
    auto submitted = submitImmediate(*m_device, "EquirectangularToCube", [&](const rhi::CommandBuffer& cmd) {
        cmd.transitionImageLayout(image, rhi::ResourceState::Unknown, rhi::ResourceState::ComputeStorage);
        cmd.bindPipeline(m_equirectToCube);
        cmd.pushConstants(constants);
        cmd.dispatch(groups(size), groups(size), 6);
        cmd.generateMipmaps(image, rhi::ResourceState::ComputeStorage);
        cmd.transitionImageLayout(image, rhi::ResourceState::CopySrc, rhi::ResourceState::ShaderRead);
    });
    m_device->free(*target);
    if (!submitted)
        return std::unexpected{ submitted.error() };
    return std::move(*cube);
}

auto Ibl::irradiance(const assets::Texture& cube) -> std::expected<std::shared_ptr<assets::Texture>, Error>
{
    auto map = assets::Texture::create(*m_device, cubeDesc("Irradiance", irradianceSize, 1));
    if (!map)
        return fromAssets(map.error(), "Failed to create irradiance map");
    auto target = storageView(**map, 0);
    if (!target)
        return std::unexpected{ target.error() };

    const IrradianceConstants constants{ cube.descriptor(), *m_device->get(*target).storageDescriptor() };

    const auto image = (*map)->image();
    auto submitted = submitImmediate(*m_device, "IrradianceConvolution", [&](const rhi::CommandBuffer& cmd) {
        cmd.transitionImageLayout(image, rhi::ResourceState::Unknown, rhi::ResourceState::ComputeStorage);
        cmd.bindPipeline(m_irradiance);
        cmd.pushConstants(constants);
        cmd.dispatch(groups(irradianceSize), groups(irradianceSize), 6);
        cmd.transitionImageLayout(image, rhi::ResourceState::ComputeStorage, rhi::ResourceState::ShaderRead);
    });
    m_device->free(*target);
    if (!submitted)
        return std::unexpected{ submitted.error() };
    return std::move(*map);
}

auto Ibl::prefiltered(const assets::Texture& cube) -> std::expected<std::shared_ptr<assets::Texture>, Error>
{
    auto map = assets::Texture::create(*m_device, cubeDesc("Prefiltered", prefilteredSize, prefilteredMipLevels));
    if (!map)
        return fromAssets(map.error(), "Failed to create prefiltered map");

    std::vector<rhi::ImageViewHandle> targets;
    const auto freeTargets = [&] {
        for (const auto view : targets)
            m_device->free(view);
    };
    for (std::uint32_t mip = 0; mip < prefilteredMipLevels; ++mip)
    {
        auto target = storageView(**map, mip);
        if (!target)
        {
            freeTargets();
            return std::unexpected{ target.error() };
        }
        targets.push_back(*target);
    }

    const auto environmentSize = static_cast<float>(m_device->get(cube.image()).extent().x);

    const auto image = (*map)->image();
    auto submitted = submitImmediate(*m_device, "PrefilterEnvironment", [&](const rhi::CommandBuffer& cmd) {
        cmd.transitionImageLayout(image, rhi::ResourceState::Unknown, rhi::ResourceState::ComputeStorage);
        cmd.bindPipeline(m_prefilter);
        for (std::uint32_t mip = 0; mip < prefilteredMipLevels; ++mip)
        {
            cmd.pushConstants(PrefilterConstants{
                .environment = cube.descriptor(),
                .prefiltered = *m_device->get(targets[mip]).storageDescriptor(),
                .roughness = static_cast<float>(mip) / static_cast<float>(prefilteredMipLevels - 1),
                .envResolution = environmentSize,
            });
            const auto size = std::max(prefilteredSize >> mip, 1u);
            cmd.dispatch(groups(size), groups(size), 6);
        }
        cmd.transitionImageLayout(image, rhi::ResourceState::ComputeStorage, rhi::ResourceState::ShaderRead);
    });
    freeTargets();
    if (!submitted)
        return std::unexpected{ submitted.error() };
    return std::move(*map);
}

auto Ibl::brdfLut() -> std::expected<std::shared_ptr<assets::Texture>, Error>
{
    auto lut = assets::Texture::create(*m_device,
        assets::Texture::Desc{
            .name = "BrdfLut",
            .extent = { brdfLutSize, brdfLutSize, 1 },
            .format = rhi::Format::RGBA16_SFLOAT,
            .mipLevels = 1,
            .sampler = { .addressMode = rhi::SamplerAddressMode::ClampToEdge },
            .usage = rhi::ImageUsage::Storage,
        });
    if (!lut)
        return fromAssets(lut.error(), "Failed to create BRDF LUT");
    auto target = storageView(**lut, 0);
    if (!target)
        return std::unexpected{ target.error() };

    const BrdfLutConstants constants{ *m_device->get(*target).storageDescriptor() };
    const auto image = (*lut)->image();
    auto submitted = submitImmediate(*m_device, "BrdfLut", [&](const rhi::CommandBuffer& cmd) {
        cmd.transitionImageLayout(image, rhi::ResourceState::Unknown, rhi::ResourceState::ComputeStorage);
        cmd.bindPipeline(m_brdfLut);
        cmd.pushConstants(constants);
        cmd.dispatch(groups(brdfLutSize), groups(brdfLutSize), 1);
        cmd.transitionImageLayout(image, rhi::ResourceState::ComputeStorage, rhi::ResourceState::ShaderRead);
    });
    m_device->free(*target);
    if (!submitted)
        return std::unexpected{ submitted.error() };
    return std::move(*lut);
}
}
