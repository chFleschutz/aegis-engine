module;

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

module aegis.test;

import std;
import aegis.renderer;
import aegis.render_graph;
import aegis.render_passes;
import aegis.rhi;

namespace aegis::test
{
// --compare: the last frame against a reference PNG from the dev GPU (tests/golden, see tests/README.md). Passes
// when no channel of any pixel is off by more than --tolerance; otherwise writes a diff image (the differing
// pixels amplified, the rest dimmed) next to --screenshot, or into the working directory, and fails.
auto Harness::compareToReference(const std::vector<std::byte>& pixels, rhi::Extent2D extent) const -> std::expected<void, std::string>
{
    int width{ 0 };
    int height{ 0 };
    int channels{ 0 };
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> reference{
        stbi_load(m_options.compare.c_str(), &width, &height, &channels, 4), &stbi_image_free
    };
    if (!reference)
        return std::unexpected{ std::format("failed to read reference '{}'", m_options.compare) };
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
        const bool over = pixelDelta > m_options.tolerance;
        differing += over;
        const auto gray = static_cast<stbi_uc>(reference.get()[i] / 4);
        diff[i] = over ? static_cast<stbi_uc>(std::min(255, 128 + pixelDelta * 4)) : gray;
        diff[i + 1] = over ? 0 : gray;
        diff[i + 2] = over ? 0 : gray;
        diff[i + 3] = 255;
    }

    if (differing == 0)
    {
        std::println("Matches reference {} (max channel difference {}, tolerance {})", m_options.compare, maxDelta, m_options.tolerance);
        return {};
    }

    auto diffPath = m_options.screenshot.empty() ? std::filesystem::path{ m_options.compare }.filename() : std::filesystem::path{ m_options.screenshot };
    diffPath.replace_extension(".diff.png");
    std::string note;
    if (stbi_write_png(diffPath.string().c_str(), width, height, 4, diff.data(), width * 4))
        note = std::format(", diff image: {}", diffPath.string());
    return std::unexpected{ std::format("frame differs from reference {}: {} pixels over tolerance {} (max channel difference {}){}",
        m_options.compare, differing, m_options.tolerance, maxDelta, note) };
}

// The renderer's offscreen target (slot 0).
auto Harness::readTarget() -> std::expected<Frame, std::string>
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

    Frame frame{ .pixels = std::vector<std::byte>(byteSize), .extent = extent };
    m_device->get(*readback).read(frame.pixels.data(), byteSize);
    m_device->free(*readback);
    return frame;
}

// --view: the view's target, the graph's "Final" (RGBA16F, left sampled by the UI pass), rounded to RGBA8.
auto Harness::readView() -> std::expected<Frame, std::string>
{
    const auto final = m_frameGraph->declarations().find(render_graph::names::finalImage);
    if (!final)
        return std::unexpected{ "the view's graph has no \"Final\"" };
    const auto& texture = m_frameGraph->pool().texture(*final);
    const auto extent = m_frameGraph->pool().extent(*final);
    if (extent != m_viewExtent)
        return std::unexpected{ std::format("the view's target is {}x{}, the view {}x{}", extent.x, extent.y, m_viewExtent.x, m_viewExtent.y) };

    constexpr std::size_t channels{ 4 };
    const auto texels = std::size_t{ extent.x } * extent.y;
    const auto byteSize = texels * channels * sizeof(std::uint16_t);
    auto readback = m_device->createBuffer({
        .name = "ViewReadback",
        .size = byteSize,
        .usage = rhi::BufferUsage::TransferDst,
        .memory = rhi::MemoryUsage::CpuRead,
    });
    if (!readback)
        return std::unexpected{ "failed to create readback buffer" };

    const std::array regions{ rhi::BufferImageCopy{ .extent = rhi::Extent3D{ extent } } };
    auto copied = submitAndWait("ViewReadback", [&](const auto& cmd) {
        cmd.transitionImageLayout(texture.view, rhi::ResourceState::ShaderReadFragment, rhi::ResourceState::CopySrc);
        cmd.copyImageToBuffer(texture.image, m_device->get(*readback), regions);
    });
    if (!copied)
        return std::unexpected{ copied.error() };

    std::vector<std::uint16_t> halves(texels * channels);
    m_device->get(*readback).read(reinterpret_cast<std::byte*>(halves.data()), byteSize);
    m_device->free(*readback);

    Frame frame{ .pixels = std::vector<std::byte>(halves.size()), .extent = extent };
    std::ranges::transform(halves, frame.pixels.begin(), [](std::uint16_t half) {
        return static_cast<std::byte>(std::lround(std::clamp(halfToFloat(half), 0.0f, 1.0f) * 255.0f));
    });
    return frame;
}

// The only proof that offscreen frames are drawn at all: read a target back and look for the
// triangle (its centre is covered and opaque, a corner only cleared, alpha 0), or in scene mode
// for an image that is not one flat color. Saves it with --screenshot.
auto Harness::verifyOffscreenFrame() -> std::expected<void, std::string>
{
    const auto frame = m_options.view ? readView() : readTarget();
    if (!frame)
        return std::unexpected{ frame.error() };
    const auto& [pixels, extent] = *frame;
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
        if (auto matched = compareToReference(pixels, extent); !matched)
            return matched;
    }

    if (m_scene)
    {
        const auto texel = [&](std::uint32_t x, std::uint32_t y) {
            const auto offset = (std::size_t{ y } * extent.x + x) * texelSize;
            return std::array{ pixels[offset], pixels[offset + 1], pixels[offset + 2] };
        };
        if (texel(extent.x / 2, extent.y / 2) == texel(0, 0))
            return std::unexpected{ "the scene frame's centre and corner have the same color" };
        std::println("Offscreen scene frame verified: {}x{} {} read back, not uniform", extent.x, extent.y, m_options.view ? "view" : "target");
        return {};
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
