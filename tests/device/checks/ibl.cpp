module aegis.test;

import std;
import aegis.paths;
import aegis.render_graph;
import aegis.render_passes;
import aegis.rhi;

namespace aegis::test
{
// IBL on the GPU from a real HDR: shapes of every map, plus values with a known answer. The
// BRDF LUT at NdotV ~ 1, roughness 0 is (~1, ~0); a lit sky's irradiance is positive.
auto Harness::verifyIbl() -> std::expected<void, std::string>
{
    auto ibl = render_graph::Ibl::create(*m_device);
    if (!ibl)
        return std::unexpected{ std::format("IBL pipelines: {}", describe(ibl.error())) };
    auto environment = ibl->loadEnvironment(paths::projectContent(paths::project()) / "environments/KloppenheimSky.hdr");
    if (!environment)
        return std::unexpected{ std::format("IBL environment: {}", describe(environment.error())) };
    auto brdfLut = ibl->brdfLut();
    if (!brdfLut)
        return std::unexpected{ std::format("IBL BRDF LUT: {}", describe(brdfLut.error())) };

    const auto check = [&](const auto& texture, std::uint32_t size, std::uint32_t mips, std::uint32_t layers,
                           std::string_view what) -> std::expected<void, std::string> {
        const auto& image = m_device->get(texture->image());
        if (image.extent().x != size || image.mipLevels() != mips || image.arrayLayers() != layers)
        {
            return std::unexpected{ std::format("{} is {}x{} with {} mips and {} layers", what, image.extent().x,
                image.extent().y, image.mipLevels(), image.arrayLayers()) };
        }
        return {};
    };
    using render_graph::Ibl;
    for (auto result : {
             check((*environment)->skybox, 512, 10, 6, "Environment cube"),
             check((*environment)->irradiance, Ibl::irradianceSize, 1, 6, "Irradiance map"),
             check((*environment)->prefiltered, Ibl::prefilteredSize, Ibl::prefilteredMipLevels, 6, "Prefiltered map"),
             check(*brdfLut, Ibl::brdfLutSize, 1, 1, "BRDF LUT"),
         })
    {
        if (!result)
            return result;
    }

    auto lut = readRgba16f((*brdfLut)->image(), Ibl::brdfLutSize, 1);
    if (!lut)
        return std::unexpected{ lut.error() };
    const auto lutTexel = [&](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
        return (*lut)[(std::size_t{ y } * Ibl::brdfLutSize + x) * 4 + c];
    };
    const auto scale = lutTexel(Ibl::brdfLutSize - 1, 0, 0);
    const auto bias = lutTexel(Ibl::brdfLutSize - 1, 0, 1);
    if (scale < 0.9f || scale > 1.05f || bias > 0.05f)
        return std::unexpected{ std::format("BRDF LUT at NdotV~1, roughness 0 is ({}, {}), want ~(1, 0)", scale, bias) };

    auto irradiance = readRgba16f((*environment)->irradiance->image(), Ibl::irradianceSize, 6);
    if (!irradiance)
        return std::unexpected{ irradiance.error() };
    const auto center = (std::size_t{ Ibl::irradianceSize / 2 } * Ibl::irradianceSize + Ibl::irradianceSize / 2) * 4;
    const auto red = (*irradiance)[center];
    if (!(red > 0.0f && red < 1000.0f))
        return std::unexpected{ std::format("Irradiance at face 0's center is {}, want a positive radiance", red) };

    std::println("IBL verified: environment cube, irradiance, prefiltered map and BRDF LUT from an HDR "
                 "(LUT ({:.3f}, {:.3f}), irradiance red {:.3f})",
        scale, bias, red);
    return {};
}

// Reads mip 0 of an RGBA16_SFLOAT image in ResourceState::ShaderRead (all layers) as floats.
auto Harness::readRgba16f(rhi::ImageHandle image, std::uint32_t size, std::uint32_t layers)
    -> std::expected<std::vector<float>, std::string>
{
    const auto texels = std::size_t{ size } * size * layers;
    auto readback = m_device->createBuffer({
        .name = "IblReadback",
        .size = texels * 8,
        .usage = rhi::BufferUsage::TransferDst,
        .memory = rhi::MemoryUsage::CpuRead,
    });
    if (!readback)
        return std::unexpected{ "Failed to create IBL readback buffer" };
    const std::array regions{ rhi::BufferImageCopy{ .arrayLayerCount = layers, .extent = { size, size, 1 } } };
    auto copied = submitAndWait("IblReadback", [&](const auto& cmd) {
        cmd.transitionImageLayout(image, rhi::ResourceState::ShaderRead, rhi::ResourceState::CopySrc);
        cmd.copyImageToBuffer(image, m_device->get(*readback), regions);
        cmd.transitionImageLayout(image, rhi::ResourceState::CopySrc, rhi::ResourceState::ShaderRead);
    });
    if (!copied)
        return std::unexpected{ copied.error() };

    std::vector<std::uint16_t> halves(texels * 4);
    m_device->get(*readback).read(reinterpret_cast<std::byte*>(halves.data()), halves.size() * 2);
    m_device->free(*readback);

    std::vector<float> values(halves.size());
    std::ranges::transform(halves, values.begin(), halfToFloat);
    return values;
}

auto Harness::halfToFloat(std::uint16_t half) -> float
{
    const auto sign = (half >> 15) ? -1.0f : 1.0f;
    const auto exponent = (half >> 10) & 0x1F;
    const auto mantissa = half & 0x3FF;
    if (exponent == 0)
        return sign * std::ldexp(static_cast<float>(mantissa), -24);
    if (exponent == 31)
        return mantissa ? std::numeric_limits<float>::quiet_NaN() : sign * std::numeric_limits<float>::infinity();
    return sign * std::ldexp(static_cast<float>(mantissa | 0x400), exponent - 25);
}
}
