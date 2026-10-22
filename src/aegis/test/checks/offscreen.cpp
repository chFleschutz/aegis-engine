module;

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC // aegis.engine has its own copy (screenshots)
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

module aegis.test;

import std;
import aegis.renderer;
import aegis.rhi;

namespace aegis::test
{
// --compare: the last frame against a reference PNG from the dev GPU (tests/golden, see tests/README.md). Passes
// when no channel of any pixel is off by more than --tolerance; otherwise writes a diff image (the differing
// pixels amplified, the rest dimmed) next to --screenshot, or into the working directory, and fails.
auto compareToReference(const Options& options, std::span<const std::byte> pixels, rhi::Extent2D extent) -> std::expected<void, std::string>
{
    int width{ 0 };
    int height{ 0 };
    int channels{ 0 };
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> reference{
        stbi_load(options.compare.c_str(), &width, &height, &channels, 4), &stbi_image_free
    };
    if (!reference)
        return std::unexpected{ std::format("failed to read reference '{}'", options.compare) };
    if (std::cmp_not_equal(width, extent.x) || std::cmp_not_equal(height, extent.y))
        return std::unexpected{ std::format("reference is {}x{}, the frame {}x{}", width, height, extent.x, extent.y) };

    std::vector<stbi_uc> diff(pixels.size());
    std::size_t differing{ 0 };
    int maxDelta{ 0 };
    for (std::size_t i = 0; i < pixels.size(); i += 4)
    {
        int pixelDelta{ 0 };
        for (std::size_t c = 0; c < 4; ++c)
            pixelDelta = std::max(pixelDelta, std::abs(std::to_integer<int>(pixels[i + c]) - int{ reference.get()[i + c] }));
        maxDelta = std::max(maxDelta, pixelDelta);
        const bool over = pixelDelta > options.tolerance;
        differing += over;
        const auto gray = static_cast<stbi_uc>(reference.get()[i] / 4);
        diff[i] = over ? static_cast<stbi_uc>(std::min(255, 128 + pixelDelta * 4)) : gray;
        diff[i + 1] = over ? 0 : gray;
        diff[i + 2] = over ? 0 : gray;
        diff[i + 3] = 255;
    }

    if (differing == 0)
    {
        std::println("Matches reference {} (max channel difference {}, tolerance {})", options.compare, maxDelta, options.tolerance);
        return {};
    }

    auto diffPath = options.screenshot.empty() ? std::filesystem::path{ options.compare }.filename() : std::filesystem::path{ options.screenshot };
    diffPath.replace_extension(".diff.png");
    std::string note;
    if (stbi_write_png(diffPath.string().c_str(), width, height, 4, diff.data(), width * 4))
        note = std::format(", diff image: {}", diffPath.string());
    return std::unexpected{ std::format("frame differs from reference {}: {} pixels over tolerance {} (max channel difference {}){}",
        options.compare, differing, options.tolerance, maxDelta, note) };
}

// The renderer's offscreen target (slot 0).
auto Harness::readTarget() -> std::expected<std::vector<std::byte>, std::string>
{
    const auto& target = m_renderer->offscreenTarget(0);
    const auto extent = m_renderer->extent();
    if (m_renderer->targetFormat() != rhi::Format::RGBA8_UNORM)
        return std::unexpected{ "the check assumes an RGBA8 target" };

    constexpr std::size_t texelSize{ 4 };
    const auto byteSize = std::size_t{ extent.x } * extent.y * texelSize;
    auto readback = m_device->createBuffer({
        .name = "OffscreenReadback",
        .size = byteSize,
        .usage = rhi::BufferUsage::TransferDst,
        .memory = rhi::MemoryUsage::CpuRead,
    });
    if (!readback)
        return std::unexpected{ "failed to create readback buffer" };

    const std::array regions{ rhi::BufferImageCopy{ .extent = rhi::Extent3D{ extent } } };
    auto copied = submitAndWait("OffscreenReadback", [&](const auto& cmd) {
        cmd.transitionImageLayout(target.view, rhi::ResourceState::ColorAttachment,
            rhi::ResourceState::CopySrc);
        cmd.copyImageToBuffer(target.image, m_device->get(*readback), regions);
    });
    if (!copied)
        return std::unexpected{ copied.error() };

    std::vector<std::byte> pixels(byteSize);
    m_device->get(*readback).read(pixels.data(), byteSize);
    m_device->free(*readback);
    return pixels;
}

// The only proof that offscreen frames are drawn at all: read the target back and look for the
// triangle (its centre is covered and opaque, a corner only cleared, alpha 0). Saves it with --screenshot.
auto Harness::verifyOffscreenFrame() -> std::expected<void, std::string>
{
    const auto read = readTarget();
    if (!read)
        return std::unexpected{ read.error() };
    const auto& pixels = *read;
    const auto extent = m_renderer->extent();
    constexpr std::size_t texelSize{ 4 };

    if (!m_options.screenshot.empty())
    {
        const auto stride = static_cast<int>(extent.x * texelSize);
        if (!stbi_write_png(m_options.screenshot.c_str(), static_cast<int>(extent.x), static_cast<int>(extent.y), 4,
                pixels.data(), stride))
            return std::unexpected{ std::format("failed to write screenshot '{}'", m_options.screenshot) };
        std::println("Screenshot saved: {}", m_options.screenshot);
    }

    if (!m_options.compare.empty())
    {
        if (auto matched = compareToReference(m_options, pixels, extent); !matched)
            return matched;
    }

    const auto alphaAt = [&](std::uint32_t x, std::uint32_t y) {
        return std::to_integer<std::uint32_t>(pixels[(std::size_t{ y } * extent.x + x) * texelSize + 3]);
    };
    const auto centre = alphaAt(extent.x / 2, extent.y / 2);
    const auto corner = alphaAt(0, 0);
    if (centre != 255 || corner != 0)
        return std::unexpected{ std::format("centre alpha {} (want 255), corner alpha {} (want 0)", centre, corner) };

    std::println("Offscreen frame verified: {}x{} target read back, triangle drawn", extent.x, extent.y);
    return {};
}
}
