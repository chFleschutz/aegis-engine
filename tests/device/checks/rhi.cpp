module aegis.test;

import std;
import aegis.core;
import aegis.rhi;

namespace aegis::test
{
// The Desc -> Vulkan mapping returns vk:: types, so no unit test can reach it; validation of these
// create calls is its only coverage, hence every enum value appears once. The reuse checks pin
// that a freed bindless slot stays reserved until the GPU retires it, then returns to the heap.
auto Harness::verifySamplers() -> std::expected<void, std::string>
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
auto Harness::verifyBufferDeviceAddress() -> std::expected<void, std::string>
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
auto Harness::verifyComputeRoundTrip() -> std::expected<void, std::string>
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

// A validation message reaches a sink added next to stderr, the way the editor console listens.
auto Harness::verifyValidationLog() -> std::expected<void, std::string>
{
    constexpr std::string_view marker = "aegis-test validation sink check";

    std::vector<std::pair<log::Level, std::string>> received;
    const auto sink = log::addSink([&](log::Level level, std::string_view message) {
        if (message.contains(marker))
            received.emplace_back(level, std::string{ message });
    });
    const bool submitted = m_context->submitDebugMessage(marker);
    log::removeSink(sink);

    if (!submitted)
    {
        std::println("Validation log check skipped: validation is off");
        return {};
    }
    if (received.size() != 1)
        return std::unexpected{ std::format("A validation message reached a log sink {} times, not once", received.size()) };
    if (received[0].first != log::Level::Warn)
        return std::unexpected{ std::format("A validation warning reached a log sink as {}", log::levelName(received[0].first)) };

    std::println("Validation log verified: a validation warning reached a log sink next to stderr");
    return {};
}
}
