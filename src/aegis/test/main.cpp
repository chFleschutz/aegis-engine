#ifdef _WIN32
// Win32 only to synthesize input events for verifyInput; other platforms skip that check.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include <imgui.h>

import std;
import aegis.assets;
import aegis.paths;
import aegis.platform;
import aegis.renderer;
import aegis.render_graph;
import aegis.rhi;
import aegis.math;
import aegis.scene;
import aegis.scene.registry;
import aegis.scene.systems.camera_system;
import aegis.scene.systems.transform_system;

using SpirvBuffer = std::vector<std::uint32_t>;

// Exit code and error text for a run the device can't do (CTest SKIP_RETURN_CODE in tests/CMakeLists.txt).
constexpr int skipExitCode{ 77 };
constexpr std::string_view skipMeshShaders{ "Skipped: --gpu-driven needs mesh shaders, which this device lacks" };

auto loadSPIRV(const std::filesystem::path& path) -> std::expected<SpirvBuffer, std::string>
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file.is_open())
        return std::unexpected{ std::format("Failed to open shader: {}", path.string()) };

    const auto fileSize = static_cast<std::size_t>(file.tellg());
    if (fileSize % sizeof(std::uint32_t) != 0)
        return std::unexpected{ "SPIR-V file size is not a multiple of 4 bytes" };

    SpirvBuffer buffer(fileSize / sizeof(std::uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(fileSize));

    if (!file)
        return std::unexpected{ "Failed to read complete shader file" };

    return buffer;
}

namespace aegis
{
struct Options
{
    bool headless{ false };
    std::uint32_t frames{ 0 }; // 0: until the window is closed
    bool scene{ false };       // Render a loaded scene through the render graph instead of the triangle
    bool gpuDriven{ false };   // With --scene: the GPU-driven (mesh shader) geometry path
    bool mutateScene{ false }; // With --scene: change the loaded scene while frames are in flight (see mutateScene())
    std::string screenshot;    // Headless: save the last frame here as PNG
    std::string compare;       // Headless: fail unless the last frame matches this reference PNG (see compareToReference())
    int tolerance{ 0 };        // With --compare: the largest per-channel difference (0-255) a pixel may have
    bool noOverlay{ false };   // With --scene: leave the stats window out, its per-frame timings change every run
};

auto parseOptions(std::span<char*> args) -> std::expected<Options, std::string>
{
    Options options;
    for (std::size_t i = 1; i < args.size(); ++i)
    {
        const std::string_view arg{ args[i] };
        if (arg == "--headless")
        {
            options.headless = true;
        }
        else if (arg == "--scene")
        {
            options.scene = true;
        }
        else if (arg == "--gpu-driven")
        {
            options.scene = true;
            options.gpuDriven = true;
        }
        else if (arg == "--mutate-scene")
        {
            options.scene = true;
            options.mutateScene = true;
        }
        else if (arg == "--screenshot" && i + 1 < args.size())
        {
            options.screenshot = args[++i];
        }
        else if (arg == "--compare" && i + 1 < args.size())
        {
            options.compare = args[++i];
        }
        else if (arg == "--tolerance" && i + 1 < args.size())
        {
            const std::string_view value{ args[++i] };
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), options.tolerance);
            if (error != std::errc{} || end != value.data() + value.size() || options.tolerance < 0 || options.tolerance > 255)
                return std::unexpected{ std::format("--tolerance needs an integer from 0 to 255, got '{}'", value) };
        }
        else if (arg == "--no-overlay")
        {
            options.noOverlay = true;
        }
        else if (arg == "--frames" && i + 1 < args.size())
        {
            const std::string_view value{ args[++i] };
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), options.frames);
            if (error != std::errc{} || end != value.data() + value.size() || options.frames == 0)
                return std::unexpected{ std::format("--frames needs a positive integer, got '{}'", value) };
        }
        else
        {
            return std::unexpected{ std::format("Unknown argument '{}'", arg) };
        }
    }

    if (!options.compare.empty() && !options.headless)
        return std::unexpected{ "--compare needs --headless" };

    // Headless has no window to close, so it always stops by itself.
    if (options.headless && options.frames == 0)
        options.frames = 600;
    return options;
}

class Engine
{
public:
    static auto create(const Options& options) -> std::expected<Engine, std::string>
    {
        Engine engine{};
        engine.m_options = options;

        if (!options.headless)
        {
            platform::Window::Desc windowDesc{
                .title = "Test Window",
                .width = 640,
                .height = 480,
            };
            engine.m_window = std::make_unique<platform::Window>(windowDesc);
            engine.m_input = std::make_unique<platform::Input>(*engine.m_window);

            if (auto result = engine.verifyInput(); !result)
                return std::unexpected{ result.error() };
        }

        auto context = rhi::Context::create({
            .appName = "TestApp",
            .window = engine.m_window.get(),
        });
        if (!context)
            return std::unexpected{ "Failed to create rhi context" };
        engine.m_context = std::move(*context);

        auto device = engine.m_context->createDevice({});
        if (!device)
            return std::unexpected{ "Failed to create rhi device" };
        if (options.gpuDriven && !(*device)->capabilities().meshShaders)
            return std::unexpected{ std::string{ skipMeshShaders } };
        engine.m_device = std::move(*device);

        auto renderer = renderer::Renderer::create({
            .context = *engine.m_context,
            .device = *engine.m_device,
            .window = engine.m_window.get(),
            .offscreen = { .extent = { 640, 480 } },
        });
        if (!renderer)
            return std::unexpected{ "Failed to create renderer" };
        engine.m_renderer = std::move(*renderer);

        auto shader = loadSPIRV(AEGIS_TEST_SHADER_PATH);
        if (!shader)
            return std::unexpected{ std::format("Failed to load shader from {}", AEGIS_TEST_SHADER_PATH) };

        auto colorAttachments = std::array{ rhi::Pipeline::ColorAttachment{ engine.m_renderer->targetFormat() } };
        auto shaders = std::array{
            rhi::Pipeline::Shader{
                .name = "TriangleVertexShader",
                .stage = rhi::ShaderStage::Vertex,
                .code = *shader,
                .entryPoint = "vertexMain",
            },
            rhi::Pipeline::Shader{
                .name = "TriangleFragmentShader",
                .stage = rhi::ShaderStage::Fragment,
                .code = *shader,
                .entryPoint = "fragmentMain",
            },
        };
        rhi::Pipeline::GraphicsDesc pipelineDesc{
            .name = "TrianglePipeline",
            .shaders = shaders,
            .colorAttachments = colorAttachments,
            .depthAttachment = rhi::Format::D32_SFLOAT,
        };
        auto pipeline = engine.m_device->createPipeline(pipelineDesc);
        if (!pipeline)
            return std::unexpected{ "Failed to create pipeline" };
        engine.m_pipeline = std::move(*pipeline);

        // Every fixed-function field away from its default, drawn each frame so validation checks the
        // state against the render pass at draw time, not only at creation. The triangle is
        // counter-clockwise, so with a clockwise front face it is back-facing and survives front culling.
        const auto overlayAttachments = std::array{
            rhi::Pipeline::ColorAttachment{ engine.m_renderer->targetFormat(), rhi::BlendMode::Alpha },
        };
        auto overlay = engine.m_device->createPipeline(rhi::Pipeline::GraphicsDesc{
            .name = "TriangleOverlayPipeline",
            .shaders = shaders,
            .colorAttachments = overlayAttachments,
            .depthAttachment = rhi::Format::D32_SFLOAT,
            .depth = { .test = true, .write = false, .compareOp = rhi::CompareOp::LessOrEqual },
            .cullMode = rhi::CullMode::Front,
            .frontFace = rhi::FrontFace::Clockwise,
        });
        if (!overlay)
            return std::unexpected{ "Failed to create overlay pipeline" };
        engine.m_overlayPipeline = std::move(*overlay);

        auto depthTexture = engine.m_renderer->createResolutionDependentTexture({
            .name = "SceneDepth",
            .extent = rhi::Extent3D{ engine.m_renderer->extent() },
            .format = rhi::Format::D32_SFLOAT,
            .usage = rhi::ImageUsage::DepthStencilAttachment,
        });
        if (!depthTexture)
            return std::unexpected{ "Failed to create depth image" };
        engine.m_depthTexture = std::move(*depthTexture);

        for (std::uint32_t i = 0; i < renderer::Renderer::maxFramesInFlight; ++i)
        {
            auto pool = engine.m_device->createQueryPool({ .name = "FrameTimestamps", .count = 2 });
            if (!pool)
                return std::unexpected{ "Failed to create timestamp query pool" };
            engine.m_frameTimers.push_back({ .pool = std::move(*pool) });
        }

        if (auto result = engine.verifyUploads(); !result)
            return std::unexpected{ result.error() };
        if (auto result = engine.verifySamplers(); !result)
            return std::unexpected{ result.error() };
        if (auto result = engine.verifyBufferDeviceAddress(); !result)
            return std::unexpected{ result.error() };
        if (auto result = engine.verifyComputeRoundTrip(); !result)
            return std::unexpected{ result.error() };
        if (auto result = engine.verifyRendererTextures(); !result)
            return std::unexpected{ result.error() };
        if (auto result = engine.verifyAssets(); !result)
            return std::unexpected{ result.error() };
        if (auto result = engine.verifyIbl(); !result)
            return std::unexpected{ result.error() };

        if (options.scene)
        {
            if (auto result = engine.createScene(); !result)
                return std::unexpected{ result.error() };
        }

        return engine;
    }

    // Smoke-tests Device::upload / flushUploads against a real device. Unit tests cover the offset
    // math; only a round-trip proves the recorded copies actually land where that math says.
    auto verifyUploads() -> std::expected<void, std::string>
    {
        if (auto result = verifyBufferUpload(); !result)
            return result;
        if (auto result = verifyOverflowUpload(); !result)
            return result;
        if (auto result = verifyImageUpload(); !result)
            return result;
        if (auto result = verifyCubemapUpload(); !result)
            return result;
        if (auto result = verifySinglePixelUploads(); !result)
            return result;
        return verifyMipGeneration();
    }

    // Mips are generated by linear blits: a blittable format must get them cleanly under validation,
    // and one that cannot be blitted (depth) must be refused up front rather than recorded.
    auto verifyMipGeneration() -> std::expected<void, std::string>
    {
        constexpr auto extent = rhi::Extent3D{ 4, 4, 1 };
        auto color = m_device->createImage({
            .name = "MipGenTestColor",
            .extent = extent,
            .format = rhi::Format::RGBA8_UNORM,
            .usage = rhi::ImageUsage::TransferDst | rhi::ImageUsage::TransferSrc | rhi::ImageUsage::Sampled,
            .mipLevels = rhi::Image::fullMipChain,
        });
        auto depth = m_device->createImage({
            .name = "MipGenTestDepth",
            .extent = extent,
            .format = rhi::Format::D32_SFLOAT,
            .usage = rhi::ImageUsage::TransferDst | rhi::ImageUsage::TransferSrc |
                     rhi::ImageUsage::DepthStencilAttachment,
            .mipLevels = rhi::Image::fullMipChain,
        });
        if (!color || !depth)
            return std::unexpected{ "Failed to create mip generation test images" };

        std::array<std::byte, 4 * 4 * 4> mip0{}; // RGBA8 and D32 are both 4 bytes per texel
        mip0.fill(std::byte{ 0x7F });

        if (!m_device->upload(*color, mip0, true))
            return std::unexpected{ "Mip generation refused a blittable format" };
        auto refused = m_device->upload(*depth, mip0, true);
        if (refused || refused.error().code != rhi::ErrorCode::FormatNotSupported)
            return std::unexpected{ "Mip generation accepted a depth format" };

        if (auto result = flushAndWait(); !result)
            return result;
        const auto mipLevels = m_device->get(*color).mipLevels();
        m_device->free(*color);
        m_device->free(*depth);

        std::println("Mip generation verified: {} mips blitted, depth refused", mipLevels);
        return {};
    }

    // The Desc -> Vulkan mapping returns vk:: types, so no unit test can reach it; validation of these
    // create calls is its only coverage, hence every enum value appears once. The reuse checks pin
    // that a freed bindless slot stays reserved until the GPU retires it, then returns to the heap.
    auto verifySamplers() -> std::expected<void, std::string>
    {
        auto linear = m_device->createSampler({ .name = "SamplerTestLinearRepeat" });
        auto nearest = m_device->createSampler({
            .name = "SamplerTestNearestClampToEdge",
            .magFilter = rhi::Filter::Nearest,
            .minFilter = rhi::Filter::Nearest,
            .mipmapMode = rhi::SamplerMipmapMode::Nearest,
            .addressMode = rhi::SamplerAddressMode::ClampToEdge,
            .maxAnisotropy = 1.0f,
        });
        auto mirrored = m_device->createSampler({
            .name = "SamplerTestMirroredRepeat",
            .addressMode = rhi::SamplerAddressMode::MirroredRepeat,
        });
        auto border = m_device->createSampler({
            .name = "SamplerTestClampToBorder",
            .addressMode = rhi::SamplerAddressMode::ClampToBorder,
            .maxLod = 0.0f,
        });
        if (!linear || !nearest || !mirrored || !border)
            return std::unexpected{ "Failed to create test samplers" };

        const auto freedIndex = m_device->get(*nearest).descriptor().index;
        if (freedIndex == m_device->get(*linear).descriptor().index)
            return std::unexpected{ "Two live samplers share a bindless slot" };

        m_device->free(*nearest);
        auto early = m_device->createSampler({ .name = "SamplerTestBeforeRetire" });
        if (!early)
            return std::unexpected{ "Failed to create sampler after freeing one" };
        if (m_device->get(*early).descriptor().index == freedIndex)
            return std::unexpected{ "A freed sampler slot was reused before the GPU retired it" };

        // Retire the free the way Renderer::beginFrame does: complete a submission and report it.
        if (auto result = submitAndWait("SamplerTestRetire", [](const auto&) {}); !result)
            return result;
        m_device->setFrameCompleted(m_device->graphicsQueue().lastSubmitted());

        auto reused = m_device->createSampler({ .name = "SamplerTestAfterRetire" });
        if (!reused)
            return std::unexpected{ "Failed to create sampler after retiring one" };
        const auto reusedIndex = m_device->get(*reused).descriptor().index;

        m_device->free(*linear);
        m_device->free(*mirrored);
        m_device->free(*border);
        m_device->free(*early);
        m_device->free(*reused);

        if (reusedIndex != freedIndex)
            return std::unexpected{
                std::format("Retired sampler slot {} was not reused (got {})", freedIndex, reusedIndex)
            };

        std::println("Samplers verified: slot {} held until retired, then reused", freedIndex);
        return {};
    }

    // Validation is the real check: it rejects an address query on a buffer missing the usage flag,
    // or on memory allocated without VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT. verifyComputeRoundTrip
    // covers a shader actually reading and writing through the addresses.
    auto verifyBufferDeviceAddress() -> std::expected<void, std::string>
    {
        constexpr rhi::Buffer::Desc desc{
            .name = "DeviceAddressTest",
            .size = 256,
            .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::ShaderDeviceAddress,
            .memory = rhi::MemoryUsage::GpuOnly,
        };
        auto first = m_device->createBuffer(desc);
        auto second = m_device->createBuffer(desc);
        if (!first || !second)
            return std::unexpected{ "Failed to create device address test buffers" };

        const auto firstAddress = m_device->get(*first).deviceAddress();
        const auto secondAddress = m_device->get(*second).deviceAddress();
        m_device->free(*first);
        m_device->free(*second);

        if (firstAddress == 0 || secondAddress == 0)
            return std::unexpected{ "Buffer device address is null" };
        if (firstAddress == secondAddress)
            return std::unexpected{ "Two live buffers share a device address" };

        std::println("Buffer device addresses verified: {:#x}, {:#x}", firstAddress, secondAddress);
        return {};
    }

    // A shader reads and writes through buffer device addresses it receives as push constants: the
    // shared pipeline layout, pushConstants, dispatch and BDA, end to end.
    auto verifyComputeRoundTrip() -> std::expected<void, std::string>
    {
        constexpr std::uint32_t count{ 1000 }; // Not a multiple of the group size: the bounds check matters
        constexpr std::uint32_t groupSize{ 64 };
        constexpr auto byteSize = count * sizeof(std::uint32_t);
        std::vector<std::uint32_t> srcData(count);
        for (std::uint32_t i = 0; i < count; ++i)
            srcData[i] = i * 7;

        auto src = m_device->createBuffer({
            .name = "ComputeTestSrc",
            .size = byteSize,
            .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::ShaderDeviceAddress |
                     rhi::BufferUsage::TransferDst,
            .memory = rhi::MemoryUsage::GpuOnly,
        });
        auto dst = m_device->createBuffer({
            .name = "ComputeTestDst",
            .size = byteSize,
            .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::ShaderDeviceAddress |
                     rhi::BufferUsage::TransferSrc,
            .memory = rhi::MemoryUsage::GpuOnly,
        });
        auto readback = m_device->createBuffer({
            .name = "ComputeTestReadback",
            .size = byteSize,
            .usage = rhi::BufferUsage::TransferDst,
            .memory = rhi::MemoryUsage::CpuRead,
        });
        if (!src || !dst || !readback)
            return std::unexpected{ "Failed to create compute test buffers" };

        auto shader = loadSPIRV(AEGIS_TEST_SHADER_PATH);
        if (!shader)
            return std::unexpected{ shader.error() };
        auto pipeline = m_device->createPipeline(rhi::Pipeline::ComputeDesc{
            .name = "ComputeTestPipeline",
            .shader = {
                .name = "ComputeTestShader",
                .stage = rhi::ShaderStage::Compute,
                .code = *shader,
                .entryPoint = "computeMain",
            },
        });
        if (!pipeline)
            return std::unexpected{ "Failed to create compute pipeline" };

        if (!m_device->upload(*src, std::as_bytes(std::span{ srcData })))
            return std::unexpected{ "UploadManager could not stage the compute test data" };
        if (auto result = flushAndWait(); !result)
            return result;

        // Matches RoundTripConstants in shader.slang.
        struct RoundTripConstants
        {
            rhi::DeviceAddress src;
            rhi::DeviceAddress dst;
            std::uint32_t count;
        };
        const RoundTripConstants constants{
            .src = m_device->get(*src).deviceAddress(),
            .dst = m_device->get(*dst).deviceAddress(),
            .count = count,
        };

        auto ran = submitAndWait("ComputeTest", [&](const auto& cmd) {
            cmd.bindPipeline(*pipeline);
            cmd.pushConstants(constants);
            cmd.dispatch((count + groupSize - 1) / groupSize);
            cmd.memoryBarrier(rhi::ResourceState::ComputeStorage, rhi::ResourceState::CopySrc);
            cmd.copyBuffer(m_device->get(*dst), m_device->get(*readback), byteSize);
        });
        if (!ran)
            return ran;

        std::vector<std::uint32_t> dstData(count);
        m_device->get(*readback).read(reinterpret_cast<std::byte*>(dstData.data()), byteSize);

        m_device->free(*src);
        m_device->free(*dst);
        m_device->free(*readback);

        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (dstData[i] != srcData[i] * 3 + 1)
            {
                return std::unexpected{
                    std::format("Compute round-trip mismatch at {}: got {}, want {}", i, dstData[i], srcData[i] * 3 + 1)
                };
            }
        }

        std::println("Compute round-trip verified: {} values read and written through device addresses", count);
        return {};
    }

    // Posts synthetic Win32 key and mouse messages to the window and lets the real event loop process
    // them, so polling and key bindings are checked end to end without anyone at the keyboard.
    // Assumes no modifier key is physically held during the run: GLFW reads modifiers from the keyboard.
    // Windows only: X11 and Wayland have no equivalent that reaches GLFW's event loop as reliably.
    auto verifyInput() -> std::expected<void, std::string>
    {
#ifndef _WIN32
        std::println("Input check skipped: synthetic input is only implemented for Win32");
        return {};
#else
        const auto hwnd = glfwGetWin32Window(m_window->glfwWindow());
        const auto postKey = [hwnd](UINT message, WPARAM virtualKey) {
            // GLFW identifies the key by the scancode in bits 16-23, then sets the state from the message.
            const auto scancode = MapVirtualKeyW(static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC);
            auto lParam = static_cast<LPARAM>(1 | (scancode << 16));
            if (message == WM_KEYUP)
                lParam |= (LPARAM{ 1 } << 30) | (LPARAM{ 1 } << 31);
            PostMessageW(hwnd, message, virtualKey, lParam);
        };

        // Shared, not 'this': the bindings outlive this call and the Engine is moved after create().
        const auto presses = std::make_shared<std::uint32_t>(0);
        m_input->bind(platform::Key::K, [presses] { ++*presses; });
        m_input->bind(platform::Key::K, [presses] { ++*presses; }, platform::KeyAction::Press,
            platform::Modifiers::Control); // Must not fire: Control is not held

        postKey(WM_KEYDOWN, 'K');
        m_window->update();
        const bool keyDown = m_input->keyPressed(platform::Key::K);
        const auto pressesAfterDown = *presses;

        postKey(WM_KEYUP, 'K');
        m_window->update();
        const bool keyUp = !m_input->keyPressed(platform::Key::K);

        PostMessageW(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, 0);
        m_window->update();
        const bool buttonDown = m_input->mouseButtonPressed(platform::MouseButton::Right);

        PostMessageW(hwnd, WM_RBUTTONUP, 0, 0);
        m_window->update();
        const bool buttonUp = !m_input->mouseButtonPressed(platform::MouseButton::Right);

        if (!keyDown || !keyUp)
            return std::unexpected{ std::format("Input: key state wrong (down {}, released {})", keyDown, keyUp) };
        if (pressesAfterDown != 1 || *presses != 1)
            return std::unexpected{
                std::format("Input: expected 1 binding call on press only, got {} then {}", pressesAfterDown,
                    *presses)
            };
        if (!buttonDown || !buttonUp)
            return std::unexpected{
                std::format("Input: mouse button state wrong (down {}, released {})", buttonDown, buttonUp)
            };

        std::println("Input verified: key state, key binding (press only, exact modifiers) and mouse button");
        return {};
#endif
    }

    // Renderer textures must reach shaders through bindless, cover all their mips, and keep their
    // bindless slot across a resize. The name is a temporary on purpose: the renderer must copy it.
    // aegis.assets against a real device: default assets, a texture read back after its upload left
    // it in ShaderRead, per-frame material blocks, and glTF + OBJ files loaded into a scene.
    auto verifyAssets() -> std::expected<void, std::string>
    {
        constexpr auto framesInFlight = renderer::Renderer::maxFramesInFlight;
        auto defaults = assets::DefaultAssets::create(*m_device, framesInFlight);
        if (!defaults)
            return std::unexpected{ std::format("Default assets: {}", defaults.error().message) };
        if (auto result = flushAndWait(); !result)
            return result;

        // The white texture: 1x1 RGBA32_SFLOAT, so 16 bytes of 1.0f.
        auto readback = m_device->createBuffer({
            .name = "AssetTestReadback",
            .size = 4 * sizeof(float),
            .usage = rhi::BufferUsage::TransferDst,
            .memory = rhi::MemoryUsage::CpuRead,
        });
        if (!readback)
            return std::unexpected{ "Failed to create asset readback buffer" };
        const std::array regions{ rhi::BufferImageCopy{ .extent = { 1, 1, 1 } } };
        auto copied = submitAndWait("AssetTestReadback", [&](const auto& cmd) {
            cmd.transitionImageLayout(defaults->white->image(), rhi::ResourceState::ShaderRead,
                rhi::ResourceState::CopySrc);
            cmd.copyImageToBuffer(defaults->white->image(), m_device->get(*readback), regions);
            cmd.transitionImageLayout(defaults->white->image(), rhi::ResourceState::CopySrc,
                rhi::ResourceState::ShaderRead);
        });
        if (!copied)
            return copied;
        std::array<float, 4> white{};
        m_device->get(*readback).read(reinterpret_cast<std::byte*>(white.data()), sizeof(white));
        m_device->free(*readback);
        if (white != std::array{ 1.0f, 1.0f, 1.0f, 1.0f })
            return std::unexpected{ std::format("Default white texture reads back as {}", white) };

        auto& material = *defaults->pbrMaterial;
        for (std::uint32_t frame = 0; frame < framesInFlight; ++frame)
            material.update(frame);
        if (material.parameterAddress(0) == 0 || material.parameterAddress(1) == material.parameterAddress(0))
            return std::unexpected{ "Material parameter blocks are null or shared between frames" };

        // Caller bugs come back as errors instead of corrupting a parameter block or over-reading.
        const auto rejected = [](const auto& result) {
            return !result && result.error().code == assets::ErrorCode::InvalidArgument;
        };
        if (!rejected(material.setParameter("roughness", 1)) ||
            !rejected(material.setParameter("albedoMap", std::shared_ptr<assets::Texture>{})))
            return std::unexpected{ "A material accepted a value of the wrong type or a null texture" };
        const std::array<float, 4> oneTexel{};
        const auto wrongSize = assets::Texture::create(*m_device,
            { .name = "WrongSize", .extent = { 2, 2, 1 }, .format = rhi::Format::RGBA32_SFLOAT, .mipLevels = 1 },
            std::as_bytes(std::span{ oneTexel }));
        if (!rejected(wrongSize))
            return std::unexpected{ "Texture::create accepted 16 bytes as mip 0 of a 2x2 RGBA32_SFLOAT texture" };

        aegis::scene::Registry scene;
        const assets::Loader::Desc loaderDesc{
            .device = *m_device,
            .scene = scene,
            .pbrTemplate = defaults->pbrTemplate,
            .defaultMaterial = defaults->pbrMaterial,
            .framesInFlight = framesInFlight,
        };
        for (const auto* model : { "DamagedHelmet/DamagedHelmet.gltf", "Misc/cube.obj" })
        {
            auto root = assets::Loader::load(loaderDesc, paths::assets() / model);
            if (!root)
                return std::unexpected{ std::format("Loading {}: {}", model, root.error().message) };
        }
        if (auto result = flushAndWait(); !result)
            return result;

        std::size_t meshes{ 0 };
        std::size_t texturedMaterials{ 0 };
        for (auto&& [entity, mesh, material] : scene.view<assets::Mesh, assets::Material>().each())
        {
            if (!mesh.staticMesh || mesh.staticMesh->indexCount() == 0 || mesh.staticMesh->meshDataAddress() == 0)
                return std::unexpected{ "A loaded mesh is empty or has no device address" };
            if (!material.instance)
                return std::unexpected{ "A loaded mesh has no material" };
            const auto* albedoMap = material.instance->parameter<std::shared_ptr<assets::Texture>>("albedoMap");
            if (!albedoMap || !*albedoMap)
                return std::unexpected{ "A loaded material has no albedo map" };
            const auto& albedo = *albedoMap;
            if (albedo != defaults->white)
            {
                // The helmet's albedo: decoded at full size, with GPU-generated mips.
                const auto& image = m_device->get(albedo->image());
                if (image.extent().x != 2048 || image.extent().y != 2048 || image.mipLevels() != 12)
                {
                    return std::unexpected{ std::format("Loaded albedo map is {}x{} with {} mips, want 2048x2048 with 12",
                        image.extent().x, image.extent().y, image.mipLevels()) };
                }
                ++texturedMaterials;
            }
            ++meshes;
        }
        if (meshes != 2 || texturedMaterials != 1)
        {
            return std::unexpected{
                std::format("Expected 2 meshes, 1 with its own albedo map; got {} and {}", meshes, texturedMaterials)
            };
        }

        std::println("Assets verified: defaults (white texture read back), material blocks, invalid parameters "
                     "and mip 0 sizes rejected, glTF + OBJ loaded ({} meshes)",
            meshes);
        return {};
    }

    // IBL on the GPU from a real HDR: shapes of every map, plus values with a known answer. The
    // BRDF LUT at NdotV ~ 1, roughness 0 is (~1, ~0); a lit sky's irradiance is positive.
    auto verifyIbl() -> std::expected<void, std::string>
    {
        auto ibl = render_graph::Ibl::create(*m_device);
        if (!ibl)
            return std::unexpected{ std::format("IBL pipelines: {}", ibl.error().message) };
        auto environment = ibl->loadEnvironment(paths::assets() / "Environments/KloppenheimSky.hdr");
        if (!environment)
            return std::unexpected{ std::format("IBL environment: {}", environment.error().message) };

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
                 check(environment->skybox, 512, 10, 6, "Environment cube"),
                 check(environment->irradiance, Ibl::irradianceSize, 1, 6, "Irradiance map"),
                 check(environment->prefiltered, Ibl::prefilteredSize, Ibl::prefilteredMipLevels, 6, "Prefiltered map"),
                 check(environment->brdfLUT, Ibl::brdfLutSize, 1, 1, "BRDF LUT"),
             })
        {
            if (!result)
                return result;
        }

        auto lut = readRgba16f(environment->brdfLUT->image(), Ibl::brdfLutSize, 1);
        if (!lut)
            return std::unexpected{ lut.error() };
        const auto lutTexel = [&](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return (*lut)[(std::size_t{ y } * Ibl::brdfLutSize + x) * 4 + c];
        };
        const auto scale = lutTexel(Ibl::brdfLutSize - 1, 0, 0);
        const auto bias = lutTexel(Ibl::brdfLutSize - 1, 0, 1);
        if (scale < 0.9f || scale > 1.05f || bias > 0.05f)
            return std::unexpected{ std::format("BRDF LUT at NdotV~1, roughness 0 is ({}, {}), want ~(1, 0)", scale, bias) };

        auto irradiance = readRgba16f(environment->irradiance->image(), Ibl::irradianceSize, 6);
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
    auto readRgba16f(rhi::ImageHandle image, std::uint32_t size, std::uint32_t layers)
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

    static auto halfToFloat(std::uint16_t half) -> float
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

    // The render graph's input: DamagedHelmet under the Kloppenheim sky, a camera and two lights,
    // and the CPU-driven frame graph to draw it.
    auto createScene() -> std::expected<void, std::string>
    {
        constexpr auto framesInFlight = renderer::Renderer::maxFramesInFlight;
        auto defaults = assets::DefaultAssets::create(*m_device, framesInFlight);
        if (!defaults)
            return std::unexpected{ std::format("Default assets: {}", defaults.error().message) };
        m_defaults = std::move(*defaults);

        m_scene = std::make_unique<aegis::scene::Scene>();
        m_scene->addSystem<aegis::scene::TransformSystem>();
        m_scene->addSystem<aegis::scene::CameraSystem>();
        m_drawBatches = std::make_unique<render_graph::DrawBatchRegistry>();
        m_drawBatches->connect(m_scene->registry());

        auto& registry = m_scene->registry();
        const auto camera = registry.create("Camera", glm::vec3{ 0.0f, -3.0f, 0.0f });
        registry.add<aegis::Camera>(camera);
        m_scene->setMainCamera(camera);

        const auto ambient = registry.create("Ambient Light");
        registry.add<aegis::AmbientLight>(ambient).intensity = 1.0f;
        m_scene->setAmbientLight(ambient);

        const auto sun = registry.create("Directional Light");
        registry.add<aegis::DirectionalLight>(sun);
        registry.get<aegis::Transform>(sun).rotation = glm::quat{ glm::radians(glm::vec3{ 60.0f, 0.0f, 45.0f }) };
        m_scene->setDirectionalLight(sun);

        // In view, off to the side of the helmet: drawn as a billboard by the transparent pass.
        const auto lamp = registry.create("Point Light", glm::vec3{ 1.2f, -1.0f, 0.6f });
        registry.add<aegis::PointLight>(lamp).color = glm::vec3{ 1.0f, 0.4f, 0.1f };

        auto ibl = render_graph::Ibl::create(*m_device);
        if (!ibl)
            return std::unexpected{ std::format("IBL: {}", ibl.error().message) };
        auto environment = ibl->loadEnvironment(paths::assets() / "Environments/KloppenheimSky.hdr");
        if (!environment)
            return std::unexpected{ std::format("Environment: {}", environment.error().message) };
        const auto sky = registry.create("Sky");
        registry.add<assets::Environment>(sky, std::move(*environment));
        m_scene->setEnvironment(sky);

        auto helmet = assets::Loader::load(
            {
                .device = *m_device,
                .scene = registry,
                .pbrTemplate = m_defaults->pbrTemplate,
                .defaultMaterial = m_defaults->pbrMaterial,
                .framesInFlight = framesInFlight,
            },
            paths::assets() / "DamagedHelmet/DamagedHelmet.gltf");
        if (!helmet)
            return std::unexpected{ std::format("Loading the helmet: {}", helmet.error().message) };
        m_helmet = *helmet;

        // No platform backend: the UI is drawn, not interacted with.
        m_imgui = ImGuiContextHandle{ ImGui::CreateContext() };
        ImGui::GetIO().IniFilename = nullptr;

        m_frameGraph = std::make_unique<render_graph::FrameGraph>(*m_device, *m_renderer);
        render_graph::addDefaultPasses(*m_frameGraph,
            {
                .gpuDriven = m_options.gpuDriven,
                .drawBatches = *m_drawBatches,
                // Not 'this': the Engine is moved after create(). Renderer and graph live on the heap.
                .buildUi = [renderer = m_renderer.get(), graph = m_frameGraph.get(), overlay = !m_options.noOverlay] { buildUi(*renderer, *graph, overlay); },
                .context = m_context.get(),
            });
        if (auto compiled = m_frameGraph->compile(); !compiled)
            return std::unexpected{ std::format("Frame graph: {}", compiled.error().message) };

        m_scene->begin();
        m_frameGraph->sceneInitialized(*m_scene);

        std::print("Frame graph compiled:");
        for (const auto* pass : m_frameGraph->passes())
            std::print(" {} |", pass->info().name);
        std::println("");
        return {};
    }

    // The UI pass's input: one window, so a screenshot shows ImGui drawn over the scene.
    static auto buildUi(const renderer::Renderer& renderer, const render_graph::FrameGraph& frameGraph, bool overlay) -> void
    {
        auto& io = ImGui::GetIO();
        const auto extent = renderer.extent();
        io.DisplaySize = ImVec2{ static_cast<float>(extent.x), static_cast<float>(extent.y) };
        ImGui::NewFrame();
        if (!overlay)
            return;
        ImGui::SetNextWindowPos(ImVec2{ 10.0f, 10.0f });
        ImGui::Begin("aegis-test", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Frame graph: %d passes", static_cast<int>(frameGraph.passes().size()));
        for (const auto& timing : frameGraph.timings())
            ImGui::Text("%-20s %.3f ms", timing.name.c_str(), timing.gpuMs);
        ImGui::End();
    }

    auto verifyRendererTextures() -> std::expected<void, std::string>
    {
        auto texture = m_renderer->createResolutionDependentTexture({
            .name = std::format("RendererTest{}", "Texture"),
            .extent = rhi::Extent3D{ m_renderer->extent() },
            .format = rhi::Format::RGBA8_UNORM,
            .usage = rhi::ImageUsage::Sampled | rhi::ImageUsage::ColorAttachment,
            .mipLevels = rhi::Image::fullMipChain,
        });
        if (!texture)
            return std::unexpected{ "Failed to create renderer texture" };

        const auto before = m_device->get(texture->view).sampledDescriptor();
        if (!before)
            return std::unexpected{ "Renderer texture has no sampled descriptor" };
        const auto mipLevels = m_device->get(texture->image).mipLevels();
        if (m_device->get(texture->view).range().mipLevelCount != mipLevels)
            return std::unexpected{ "Renderer texture view does not cover the image's mips" };

        if (auto resized = m_renderer->resize(m_renderer->extent()); !resized)
            return std::unexpected{ "Failed to resize the renderer" };

        const auto after = m_device->get(texture->view).sampledDescriptor();
        if (!after || after->index != before->index)
            return std::unexpected{ "Renderer texture lost its bindless slot across a resize" };
        if (m_device->get(texture->view).range().mipLevelCount != m_device->get(texture->image).mipLevels())
            return std::unexpected{ "Resized renderer texture view does not cover the image's mips" };

        std::println("Renderer textures verified: {} mips, bindless slot {} kept across a resize",
            mipLevels, after->index);
        return {};
    }

    // --mutate-scene: changes the scene the way an editor or gameplay code would, while frames are in
    // flight, and checks that the GPU-driven path follows. Called once per frame before it is recorded.
    // Frame 30 loads a second model, 60 destroys the first one, 90 edits the second one's materials.
    // The frame after each change, the instance pass must have written as many static instances as
    // the scene holds. The last frame (--screenshot) shows the second model only, in red.
    auto mutateScene(std::uint32_t frame) -> std::expected<void, std::string>
    {
        constexpr std::uint32_t loadFrame{ 30 };
        constexpr std::uint32_t destroyFrame{ 60 };
        constexpr std::uint32_t editFrame{ 90 };
        auto& registry = m_scene->registry();

        const auto drawableStatic = [&] {
            std::uint32_t count{ 0 };
            for (auto&& [entity, mesh, material] : registry.view<assets::Mesh, assets::Material>().each())
            {
                if (mesh.staticMesh && material.instance && !registry.has<aegis::DynamicTag>(aegis::scene::Entity{ entity }))
                    ++count;
            }
            return count;
        };
        const auto writtenStatic = [&]() -> std::optional<std::uint32_t> {
            for (auto* pass : m_frameGraph->passes())
            {
                if (const auto* instances = dynamic_cast<render_graph::InstanceUpdatePass*>(pass))
                    return instances->staticCount();
            }
            return std::nullopt; // The CPU-driven path has no instance pass
        };
        // The frame after a change: the instances written for it match the scene.
        const auto verifyWritten = [&](std::string_view what, bool grew) -> std::expected<void, std::string> {
            const auto expected = drawableStatic();
            if (grew ? expected <= m_staticDrawn : expected >= m_staticDrawn)
                return std::unexpected{ std::format("{} did not change the scene's static instances ({} before, {} after)", what, m_staticDrawn, expected) };
            if (const auto written = writtenStatic(); written && *written != expected)
                return std::unexpected{ std::format("after {}: {} static instances written, the scene has {}", what, *written, expected) };
            std::println("Scene change verified: {} -> {} static instances after {}", m_staticDrawn, expected, what);
            m_staticDrawn = expected;
            return {};
        };

        if (frame == loadFrame)
        {
            m_staticDrawn = drawableStatic();
            auto model = assets::Loader::load(
                {
                    .device = *m_device,
                    .scene = registry,
                    .pbrTemplate = m_defaults->pbrTemplate,
                    .defaultMaterial = m_defaults->pbrMaterial,
                    .framesInFlight = renderer::Renderer::maxFramesInFlight,
                },
                paths::assets() / "SciFiHelmet/SciFiHelmet.gltf");
            if (!model)
                return std::unexpected{ std::format("Loading the second model: {}", model.error().message) };
            m_addedModel = *model;
            registry.get<aegis::Transform>(m_addedModel).location = glm::vec3{ 0.9f, 0.0f, 0.0f };
            registry.markStaticDirty(m_addedModel);
        }
        else if (frame == loadFrame + 1)
        {
            return verifyWritten("loading a model", true);
        }
        else if (frame == destroyFrame)
        {
            std::vector<aegis::scene::Entity> doomed{ m_helmet };
            for (std::size_t i = 0; i < doomed.size(); ++i)
            {
                for (auto child = registry.get<aegis::Children>(doomed[i]).first; child;
                    child = registry.get<aegis::Siblings>(child).next)
                    doomed.push_back(child);
            }
            for (const auto entity : doomed)
                registry.destroy(entity);
        }
        else if (frame == destroyFrame + 1)
        {
            return verifyWritten("destroying a model", false);
        }
        else if (frame == editFrame)
        {
            std::uint32_t edited{ 0 };
            for (auto&& [entity, material] : registry.view<assets::Material>().each())
            {
                if (material.instance && material.instance->setParameter("albedo", glm::vec3{ 1.0f, 0.1f, 0.1f }))
                    ++edited;
            }
            if (edited == 0)
                return std::unexpected{ "no material to edit" };
        }
        return {};
    }

    auto run() -> int
    {
        std::uint32_t frames{ 0 };
        while (m_options.frames == 0 || frames < m_options.frames)
        {
            if (m_window)
            {
                if (m_window->shouldClose())
                    break;

                m_window->update();
                if (m_window->isMinimized())
                {
                    m_window->waitEvents();
                    continue;
                }

                if (m_window->wasResized() || m_renderer->needsResize())
                {
                    if (auto resized = m_renderer->resize(rhi::Extent2D{ m_window->extent() }); !resized)
                    {
                        std::println("Renderer resize failed (renderer error {})", std::to_underlying(resized.error()));
                        m_device->waitIdle();
                        return 1;
                    }
                    m_window->resetResized();
                    if (m_frameGraph)
                    {
                        if (auto resized = m_frameGraph->resized(); !resized)
                            std::println("Frame graph resize failed: {}", resized.error().message);
                    }
                }
            }

            // Nothing queues uploads per frame yet; drain anyway so the path stays exercised.
            std::ignore = m_device->flushUploads();

            if (m_scene)
                m_scene->update(1.0f / 60.0f);

            if (m_options.mutateScene)
            {
                if (auto mutated = mutateScene(frames); !mutated)
                {
                    std::println("Scene change failed: {}", mutated.error());
                    m_device->waitIdle();
                    return 1;
                }
            }

            // Headless scene runs resize halfway, so the graph's resolution-dependent textures and the
            // views passes derive from them are recreated under validation.
            if (m_frameGraph && m_renderer->offscreen() && frames == m_options.frames / 2)
            {
                if (auto resized = m_renderer->resize({ 800, 600 }); !resized)
                {
                    std::println("Renderer resize failed (renderer error {})", std::to_underlying(resized.error()));
                    return 1;
                }
                if (auto resized = m_frameGraph->resized(); !resized)
                {
                    std::println("Frame graph resize failed: {}", resized.error().message);
                    return 1;
                }
            }

            if (auto rendered = m_renderer->renderFrame([this](const auto& frameInfo) { drawFrame(frameInfo); }); !rendered)
            {
                std::println("Rendering a frame failed (renderer error {})", std::to_underlying(rendered.error()));
                m_device->waitIdle();
                return 1;
            }
            ++frames;
        }

        m_device->waitIdle();

        auto result = reportFrameTimes();
        if (m_frameGraph)
        {
            // The frame graph's own per-pass timestamps: one entry per pass, read back once complete.
            const auto timings = m_frameGraph->timings();
            if (timings.size() != m_frameGraph->passes().size())
            {
                std::println("Pass timings failed: {} timings for {} passes", timings.size(), m_frameGraph->passes().size());
                result = 1;
            }
            for (const auto& [name, gpuMs, cpuMs] : timings)
                std::println("  {:<20} GPU {:7.4f} ms  CPU {:7.4f} ms", name, gpuMs, cpuMs);
        }
        if (m_renderer->offscreen())
        {
            if (auto offscreen = verifyOffscreenFrame(); !offscreen)
            {
                std::println("Offscreen frame check failed: {}", offscreen.error());
                result = 1;
            }
        }
        if (const auto errors = rhi::Context::validationErrorCount(); errors > 0)
        {
            std::println("Validation failed: {} errors reported (see stderr)", errors);
            return 1;
        }
        return result;
    }

    auto drawFrame(const renderer::Renderer::FrameInfo& frameInfo) -> void
    {
        const auto& [cmd, target, frameIndex, extent] = frameInfo;

        // beginFrame waited for the frame that last used this slot, so its timestamps are final.
        auto& timer = m_frameTimers[frameIndex];
        if (timer.pending)
            recordFrameTime(timer);

        cmd.resetQueries(timer.pool, 0, 2);
        if (m_frameGraph)
        {
            cmd.writeTimestamp(timer.pool, 0, rhi::TimestampStage::TopOfPipe);
            m_frameGraph->execute({
                .cmd = cmd,
                .scene = *m_scene,
                .drawBatches = *m_drawBatches,
                .frameIndex = frameIndex,
                .extent = extent,
                .target = target,
            });
            cmd.writeTimestamp(timer.pool, 1, rhi::TimestampStage::AllCommands);
            timer.pending = true;
            return;
        }

        cmd.transitionImageLayout(
            m_depthTexture.view,
            rhi::ResourceState::Unknown,
            rhi::ResourceState::DepthWrite);

        cmd.beginLabel("Rendering");
        cmd.writeTimestamp(timer.pool, 0, rhi::TimestampStage::TopOfPipe);

        std::array colorAttachments{
            rhi::Attachment::color(
                target,
                rhi::ClearColor{ 0.0, 0.0, 0.0, 0.0 })
        };
        cmd.beginRendering(rhi::RenderingCmd{
            .colorAttachments = colorAttachments,
            .depthAttachment = rhi::Attachment::depth(
                m_depthTexture.view,
                rhi::ClearDepthStencil{ 1.0f, 0 }),
        });
        cmd.bindPipeline(*m_pipeline);
        cmd.setViewport(extent);
        cmd.setScissor(extent);
        cmd.draw(3);
        cmd.bindPipeline(*m_overlayPipeline);
        cmd.draw(3);
        cmd.endRendering();
        cmd.writeTimestamp(timer.pool, 1, rhi::TimestampStage::AllCommands);
        timer.pending = true;

        cmd.endLabel();
    }

private:
    // One pool per frame in flight, so a frame's timestamps are read only after the GPU finished it.
    struct FrameTimer
    {
        rhi::QueryPool pool;
        bool pending{ false };
    };

    auto recordFrameTime(FrameTimer& timer) -> void
    {
        timer.pending = false;
        auto timestamps = timer.pool.readTimestamps(0, 2);
        if (!timestamps)
        {
            ++m_frameTimeErrors;
            return;
        }
        m_frameTimeTotalMs += timer.pool.elapsedMs((*timestamps)[0], (*timestamps)[1]);
        ++m_timedFrames;
    }

    // The exit criterion for rhi:query: every frame's rendering pass was timed and read back.
    auto reportFrameTimes() const -> int
    {
        if (m_frameTimeErrors > 0 || m_timedFrames == 0)
        {
            std::println("Timed pass failed: {} frames read back, {} reads not ready",
                m_timedFrames, m_frameTimeErrors);
            return 1;
        }

        std::println("Timed pass verified: {} frames, {:.4f} ms average GPU time",
            m_timedFrames, m_frameTimeTotalMs / m_timedFrames);
        return 0;
    }

    Engine() = default;

    // --compare: the last frame against a reference PNG from the dev GPU (tests/golden, see tests/README.md). Passes
    // when no channel of any pixel is off by more than --tolerance; otherwise writes a diff image (the differing
    // pixels amplified, the rest dimmed) next to --screenshot, or into the working directory, and fails.
    auto compareToReference(const std::vector<std::byte>& pixels, rhi::Extent2D extent) const -> std::expected<void, std::string>
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

    // The only proof that offscreen frames are drawn at all: read a target back and look for the
    // triangle (its centre is covered and opaque, a corner only cleared, alpha 0), or in scene mode
    // for an image that is not one flat color. Saves it with --screenshot.
    auto verifyOffscreenFrame() -> std::expected<void, std::string>
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
            return copied;

        std::vector<std::byte> pixels(byteSize);
        m_device->get(*readback).read(pixels.data(), byteSize);
        m_device->free(*readback);

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
            std::println("Offscreen scene frame verified: {}x{} target read back, not uniform", extent.x, extent.y);
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

    // Records 'record' into a one-shot command buffer, submits it, and waits for completion.
    auto submitAndWait(std::string_view name, auto&& record) -> std::expected<void, std::string>
    {
        auto pool = m_device->createCommandPool({
            .name = name,
            .queueFamily = m_device->graphicsQueue().family(),
        });
        if (!pool)
            return std::unexpected{ std::format("{}: failed to create command pool", name) };

        auto cmd = m_device->createCommandBuffer({ .name = name, .pool = *pool });
        if (!cmd)
            return std::unexpected{ std::format("{}: failed to create command buffer", name) };

        cmd->begin(true);
        record(*cmd);
        cmd->end();

        auto timeline = m_device->graphicsQueue().submit(*cmd);
        if (!timeline)
            return std::unexpected{ std::format("{}: failed to submit", name) };
        m_device->graphicsQueue().wait(*timeline);
        return {};
    }

    // Drains the open upload batch and blocks until the GPU has consumed it.
    auto flushAndWait() -> std::expected<void, std::string>
    {
        auto timeline = m_device->flushUploads();
        if (!timeline)
            return std::unexpected{ "flushUploads did not submit a batch" };
        m_device->graphicsQueue().wait(*timeline);
        return {};
    }

    auto verifyBufferUpload() -> std::expected<void, std::string>
    {
        constexpr std::array<std::uint32_t, 4> srcData{
            0xDEADBEEF, 0x12345678, 0xCAFEBABE, 0x0BADF00D
        };
        constexpr auto byteSize = srcData.size() * sizeof(std::uint32_t);

        auto deviceLocal = m_device->createBuffer({
            .name = "UploadTestDeviceLocal",
            .size = byteSize,
            .usage = rhi::BufferUsage::TransferDst | rhi::BufferUsage::TransferSrc,
            .memory = rhi::MemoryUsage::GpuOnly,
        });
        if (!deviceLocal)
            return std::unexpected{ "Failed to create device-local upload target" };

        auto readback = m_device->createBuffer({
            .name = "UploadTestReadback",
            .size = byteSize,
            .usage = rhi::BufferUsage::TransferDst,
            .memory = rhi::MemoryUsage::CpuRead,
        });
        if (!readback)
            return std::unexpected{ "Failed to create readback buffer" };

        if (!m_device->upload(*deviceLocal, std::as_bytes(std::span{ srcData })))
            return std::unexpected{ "UploadManager could not stage the buffer test data" };
        if (auto result = flushAndWait(); !result)
            return result;

        auto copied = submitAndWait("UploadTestBufferReadback", [&](const auto& cmd) {
            cmd.copyBuffer(m_device->get(*deviceLocal), m_device->get(*readback), byteSize);
        });
        if (!copied)
            return copied;

        std::array<std::uint32_t, 4> dstData{};
        m_device->get(*readback).read(reinterpret_cast<std::byte*>(dstData.data()), byteSize);

        m_device->free(*deviceLocal);
        m_device->free(*readback);

        if (dstData != srcData)
            return std::unexpected{ "Buffer upload readback mismatch" };

        std::println("Buffer upload verified: {} bytes match after round-trip", byteSize);
        return {};
    }

    // Larger than the whole 64 MB staging ring. Before the overflow path this could never be staged,
    // and the documented flush-and-retry response looped forever.
    auto verifyOverflowUpload() -> std::expected<void, std::string>
    {
        constexpr std::size_t count{ 20 * 1024 * 1024 };
        constexpr auto byteSize = count * sizeof(std::uint32_t);
        std::vector<std::uint32_t> srcData(count);
        for (std::size_t i = 0; i < count; ++i)
            srcData[i] = static_cast<std::uint32_t>(i * 2654435761u);

        auto deviceLocal = m_device->createBuffer({
            .name = "OverflowTestDeviceLocal",
            .size = byteSize,
            .usage = rhi::BufferUsage::TransferDst | rhi::BufferUsage::TransferSrc,
            .memory = rhi::MemoryUsage::GpuOnly,
        });
        auto readback = m_device->createBuffer({
            .name = "OverflowTestReadback",
            .size = byteSize,
            .usage = rhi::BufferUsage::TransferDst,
            .memory = rhi::MemoryUsage::CpuRead,
        });
        if (!deviceLocal || !readback)
            return std::unexpected{ "Failed to create overflow test buffers" };

        if (!m_device->upload(*deviceLocal, std::as_bytes(std::span{ srcData })))
            return std::unexpected{ "UploadManager could not stage an upload larger than its ring" };
        if (auto result = flushAndWait(); !result)
            return result;

        auto copied = submitAndWait("OverflowTestReadback", [&](const auto& cmd) {
            cmd.copyBuffer(m_device->get(*deviceLocal), m_device->get(*readback), byteSize);
        });
        if (!copied)
            return copied;

        std::vector<std::uint32_t> dstData(count);
        m_device->get(*readback).read(reinterpret_cast<std::byte*>(dstData.data()), byteSize);

        m_device->free(*deviceLocal);
        m_device->free(*readback);

        if (dstData != srcData)
            return std::unexpected{ "Overflow upload readback mismatch" };

        std::println("Overflow upload verified: {} MB, larger than the staging ring", byteSize >> 20);
        return {};
    }

    // Uploads a mipped 2D texture, giving every mip a distinct fill so that a wrong per-mip offset
    // shows up as a mismatch rather than passing on identical bytes.
    auto verifyImageUpload() -> std::expected<void, std::string>
    {
        constexpr auto extent = rhi::Extent3D{ 4, 4, 1 };
        constexpr auto format = rhi::Format::RGBA8_UNORM;
        constexpr std::uint32_t mipLevels{ 3 };
        constexpr std::array<std::byte, mipLevels> mipFill{
            std::byte{ 0xAA }, std::byte{ 0xBB }, std::byte{ 0xCC }
        };

        return verifyTextureRoundTrip("UploadTestImage", rhi::ImageType::Dim2D, extent, format, mipLevels, 1,
            [&](const auto& footprint, std::size_t /*layer*/) { return mipFill[footprint.mipLevel]; });
    }

    // The only case that exercises the array-layer packing contract: layers must sit inside each
    // mip, not after the whole chain. loadDefaultAssets()'s solidColorCube will depend on this.
    auto verifyCubemapUpload() -> std::expected<void, std::string>
    {
        constexpr auto extent = rhi::Extent3D{ 2, 2, 1 };
        constexpr auto format = rhi::Format::RGBA8_UNORM;

        return verifyTextureRoundTrip("UploadTestCubemap", rhi::ImageType::Cube, extent, format, 1, 6,
            [](const auto& /*footprint*/, std::size_t layer) {
                return static_cast<std::byte>(0x10 + layer);
            });
    }

    // 1x1 images are what solidColor / solidColorCube produce. Image type used to be inferred from the
    // extent, which made them 1D; the cube case would then fail validation on its cube view.
    auto verifySinglePixelUploads() -> std::expected<void, std::string>
    {
        constexpr auto extent = rhi::Extent3D{ 1, 1, 1 };
        constexpr auto format = rhi::Format::RGBA8_UNORM;

        auto pixel = verifyTextureRoundTrip("UploadTestPixel", rhi::ImageType::Dim2D, extent, format, 1, 1,
            [](const auto& /*footprint*/, std::size_t /*layer*/) { return std::byte{ 0x5A }; });
        if (!pixel)
            return pixel;

        return verifyTextureRoundTrip("UploadTestPixelCube", rhi::ImageType::Cube, extent, format, 1, 6,
            [](const auto& /*footprint*/, std::size_t layer) {
                return static_cast<std::byte>(0x20 + layer);
            });
    }

    // Fills every subresource via 'fillFor', uploads it, reads the image back, and compares. Also
    // creates a default-typed sampled view, so validation checks the view against the image type.
    auto verifyTextureRoundTrip(std::string_view name, rhi::ImageType type, rhi::Extent3D extent,
        rhi::Format format, std::uint32_t mipLevels, std::uint32_t arrayLayers, auto&& fillFor)
        -> std::expected<void, std::string>
    {
        const auto footprints = rhi::detail::subresourceFootprints(extent, format, mipLevels,
            arrayLayers);
        if (footprints.empty())
            return std::unexpected{ std::format("{}: no footprints for this description", name) };

        const auto& last = footprints.back();
        const auto sourceSize = last.sourceOffset + last.size;
        const auto stagingSize = last.stagingOffset + last.size;

        // Source data is tightly packed, mip-major, layers inside each mip.
        std::vector<std::byte> srcData(sourceSize);
        for (const auto& footprint : footprints)
        {
            const auto layerSize = footprint.size / footprint.arrayLayerCount;
            for (std::uint32_t layer = 0; layer < footprint.arrayLayerCount; ++layer)
            {
                const auto begin = srcData.begin() +
                                   static_cast<std::ptrdiff_t>(footprint.sourceOffset + layer * layerSize);
                std::fill_n(begin, layerSize, fillFor(footprint, layer));
            }
        }

        auto image = m_device->createImage({
            .name = name,
            .type = type,
            .extent = extent,
            .format = format,
            .usage = rhi::ImageUsage::TransferDst | rhi::ImageUsage::TransferSrc |
                     rhi::ImageUsage::Sampled,
            .mipLevels = mipLevels,
            .arrayLayers = arrayLayers,
        });
        if (!image)
            return std::unexpected{ std::format("{}: failed to create image", name) };

        auto view = m_device->createImageView(*image, {
                                                          .name = name,
                                                          .extent = extent,
                                                          .format = format,
                                                          .range = { .mipLevelCount = mipLevels, .arrayLayerCount = arrayLayers },
                                                          .usage = rhi::ImageUsage::Sampled,
                                                      });
        if (!view)
            return std::unexpected{ std::format("{}: failed to create view", name) };
        m_device->free(*view);

        auto readback = m_device->createBuffer({
            .name = "UploadTestImageReadback",
            .size = stagingSize,
            .usage = rhi::BufferUsage::TransferDst,
            .memory = rhi::MemoryUsage::CpuRead,
        });
        if (!readback)
            return std::unexpected{ std::format("{}: failed to create readback buffer", name) };

        if (!m_device->upload(*image, srcData))
            return std::unexpected{ std::format("{}: UploadManager could not stage the image", name) };
        if (auto result = flushAndWait(); !result)
            return result;

        std::vector<rhi::BufferImageCopy> regions;
        regions.reserve(footprints.size());
        for (const auto& footprint : footprints)
        {
            regions.emplace_back(rhi::BufferImageCopy{
                .bufferOffset = footprint.stagingOffset,
                .mipLevel = footprint.mipLevel,
                .baseArrayLayer = 0,
                .arrayLayerCount = footprint.arrayLayerCount,
                .extent = footprint.extent,
            });
        }

        // upload() leaves the image in CopyDst by contract -- the read-state transition is ours.
        auto copied = submitAndWait(name, [&](const auto& cmd) {
            cmd.transitionImageLayout(*image, rhi::ResourceState::CopyDst,
                rhi::ResourceState::CopySrc);
            cmd.copyImageToBuffer(*image, m_device->get(*readback), regions);
        });
        if (!copied)
            return copied;

        std::vector<std::byte> dstData(stagingSize);
        m_device->get(*readback).read(dstData.data(), stagingSize);

        m_device->free(*image);
        m_device->free(*readback);

        for (const auto& footprint : footprints)
        {
            const auto expected = std::span{ srcData }.subspan(footprint.sourceOffset, footprint.size);
            const auto actual = std::span{ dstData }.subspan(footprint.stagingOffset, footprint.size);
            if (!std::ranges::equal(expected, actual))
            {
                return std::unexpected{
                    std::format("{}: mip {} mismatch after round-trip", name, footprint.mipLevel)
                };
            }
        }

        std::println("{} verified: {} mip(s) x {} layer(s), {} bytes match after round-trip", name,
            mipLevels, arrayLayers, sourceSize);
        return {};
    }

    Options m_options;
    std::unique_ptr<platform::Window> m_window; // null when headless
    std::unique_ptr<platform::Input> m_input;   // null when headless
    std::unique_ptr<rhi::Context> m_context;
    std::unique_ptr<rhi::Device> m_device;
    std::unique_ptr<renderer::Renderer> m_renderer;

    std::optional<rhi::Pipeline> m_pipeline;
    std::optional<rhi::Pipeline> m_overlayPipeline;
    renderer::Texture m_depthTexture;
    std::vector<FrameTimer> m_frameTimers;
    std::uint32_t m_timedFrames{ 0 };
    std::uint32_t m_frameTimeErrors{ 0 };
    double m_frameTimeTotalMs{ 0.0 };

    // --scene only. Declared last: destroyed first, while the device they free into still exists.
    std::optional<assets::DefaultAssets> m_defaults;
    std::unique_ptr<aegis::scene::Scene> m_scene;
    aegis::scene::Entity m_helmet;                                  // The root of the model loaded with the scene
    aegis::scene::Entity m_addedModel;                              // --mutate-scene: the root of the model loaded later
    std::uint32_t m_staticDrawn{ 0 };                               // --mutate-scene: drawable static entities before the last change
    std::unique_ptr<render_graph::DrawBatchRegistry> m_drawBatches; // Disconnects before the scene goes
    struct ImGuiContextDeleter
    {
        auto operator()(ImGuiContext* context) const -> void { ImGui::DestroyContext(context); }
    };
    using ImGuiContextHandle = std::unique_ptr<ImGuiContext, ImGuiContextDeleter>;
    ImGuiContextHandle m_imgui; // Outlives the frame graph's UI pass
    std::unique_ptr<render_graph::FrameGraph> m_frameGraph;
};
}

auto main(int argc, char** argv) -> int
{
    const auto options = aegis::parseOptions(std::span{ argv, static_cast<std::size_t>(argc) });
    if (!options)
    {
        std::println("{}\nUsage: aegis-test [--headless] [--frames <n>] [--scene] [--gpu-driven] [--mutate-scene] [--no-overlay] [--screenshot <png>] [--compare <png> [--tolerance <0-255>]]", options.error());
        return 2;
    }

    auto engine = aegis::Engine::create(*options);
    if (!engine)
    {
        if (engine.error() == skipMeshShaders)
        {
            std::println("{}", engine.error());
            return skipExitCode;
        }
        std::println("Failed to create engine \n{}", engine.error());
        return 1;
    }
    return engine->run();
}
