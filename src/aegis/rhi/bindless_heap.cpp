module;
#include <array>
#include <cstdint>
#include <expected>

module aegis.rhi;
import :bindless_heap;
import :device;
import :vulkan_conversions;

namespace aegis::rhi
{
auto BindlessHeap::create(const vk::raii::Device& device,
    const Desc& desc) -> std::expected<BindlessHeap, Error>
{
    auto layout = createLayout(device, desc);
    if (!layout)
        return std::unexpected{ layout.error() };

    auto pool = createPool(device, desc);
    if (!pool)
        return std::unexpected{ pool.error() };

    auto set = createSet(device, *pool, *layout);
    if (!set)
        return std::unexpected{ set.error() };

    return BindlessHeap{
        desc,
        std::move(*pool),
        std::move(*layout),
        std::move(*set),
    };
}

auto BindlessHeap::binding(DescriptorType type) -> std::uint32_t
{
    switch (type)
    {
    case DescriptorType::SampledImage:
        return SampledImagesBinding;
    case DescriptorType::StorageImage:
        return StorageImagesBinding;
    case DescriptorType::Sampler:
        return SamplersBinding;
    }
    std::unreachable();
}

auto BindlessHeap::writeSampledImage(const Device& device, const ImageView& view)
    -> std::expected<SampledImageHandle, BindlessError>
{
    auto index = m_freeSampledImages.pop();
    if (!index)
        return std::unexpected{ BindlessError::OutOfMemory };

    updateDescriptorSet(device,
        DescriptorType::SampledImage,
        *index,
        vk::DescriptorImageInfo{
            .imageView = view.vk(),
            .imageLayout = vk::ImageLayout::eReadOnlyOptimal
        });

    return SampledImageHandle{ *index };
}

auto BindlessHeap::writeStorageImage(const Device& device, const ImageView& view)
    -> std::expected<StorageImageHandle, BindlessError>
{
    auto index = m_freeStorageImages.pop();
    if (!index)
        return std::unexpected{ BindlessError::OutOfMemory };

    updateDescriptorSet(device,
        DescriptorType::StorageImage,
        *index,
        vk::DescriptorImageInfo{ .imageView = view.vk(), .imageLayout = vk::ImageLayout::eGeneral });

    return StorageImageHandle{ *index };
}

auto BindlessHeap::writeSampler(const Device& device,
    vk::Sampler sampler) -> std::expected<SamplerHandle, BindlessError>
{
    auto index = m_freeSamplers.pop();
    if (!index)
        return std::unexpected{ BindlessError::OutOfMemory };

    updateDescriptorSet(device,
        DescriptorType::Sampler,
        *index,
        vk::DescriptorImageInfo{ .sampler = sampler });

    return SamplerHandle{ *index };
}

BindlessHeap::BindlessHeap(const Desc& desc, vk::raii::DescriptorPool&& pool,
    vk::raii::DescriptorSetLayout&& layout, vk::raii::DescriptorSet&& set) :
    m_pool{ std::move(pool) },
    m_layout{ std::move(layout) },
    m_set{ std::move(set) },
    m_freeSampledImages{ desc.maxSampledImages },
    m_freeStorageImages{ desc.maxStorageImages },
    m_freeSamplers{ desc.maxSamplers }
{
}

auto BindlessHeap::createLayout(const vk::raii::Device& device,
    const Desc& desc) -> std::expected<vk::raii::DescriptorSetLayout, Error>
{
    auto bindings = std::array{
        vk::DescriptorSetLayoutBinding{
            .binding = SampledImagesBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = desc.maxSampledImages,
            .stageFlags = vk::ShaderStageFlagBits::eAll,
        },
        vk::DescriptorSetLayoutBinding{
            .binding = StorageImagesBinding,
            .descriptorType = vk::DescriptorType::eStorageImage,
            .descriptorCount = desc.maxStorageImages,
            .stageFlags = vk::ShaderStageFlagBits::eAll,
        },
        vk::DescriptorSetLayoutBinding{
            .binding = SamplersBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = desc.maxSamplers,
            .stageFlags = vk::ShaderStageFlagBits::eAll,
        },
    };

    std::array<vk::DescriptorBindingFlags, bindings.size()> flags;
    flags.fill(
        vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind
    );

    vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
        .bindingCount = static_cast<uint32_t>(flags.size()),
        .pBindingFlags = flags.data(),
    };

    vk::DescriptorSetLayoutCreateInfo layoutInfo{
        .pNext = &flagsInfo,
        .flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };

    auto layout = device.createDescriptorSetLayout(layoutInfo);
    if (!layout.has_value())
        return makeError(toRHI(layout.result));

    return std::move(*layout);
}

auto BindlessHeap::createPool(const vk::raii::Device& device,
    const Desc& desc) -> std::expected<vk::raii::DescriptorPool, Error>
{
    std::array poolSizes{
        vk::DescriptorPoolSize{
            .type = vk::DescriptorType::eSampledImage,
            .descriptorCount = desc.maxSampledImages,
        },
        vk::DescriptorPoolSize{
            .type = vk::DescriptorType::eStorageImage,
            .descriptorCount = desc.maxStorageImages,
        },
        vk::DescriptorPoolSize{
            .type = vk::DescriptorType::eSampler,
            .descriptorCount = desc.maxSamplers,
        },
    };

    vk::DescriptorPoolCreateInfo poolInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
        .maxSets = 1,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };

    auto pool = device.createDescriptorPool(poolInfo);
    if (!pool.has_value())
        return makeError(toRHI(pool.result));

    return std::move(*pool);
}

auto BindlessHeap::createSet(const vk::raii::Device& device, vk::DescriptorPool pool,
    vk::DescriptorSetLayout layout) -> std::expected<vk::raii::DescriptorSet, Error>
{
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &layout,
    };

    auto set = device.allocateDescriptorSets(allocInfo);
    if (!set.has_value() || set->empty())
        return makeError(toRHI(set.result));

    return std::move(set->front());
}

auto BindlessHeap::updateDescriptorSet(const Device& device, DescriptorType type, std::uint32_t index,
    const vk::DescriptorImageInfo& info) const -> void
{
    vk::WriteDescriptorSet write{
        .dstSet = *m_set,
        .dstBinding = binding(type),
        .dstArrayElement = index,
        .descriptorCount = 1,
        .descriptorType = toVulkan(type),
        .pImageInfo = &info,
    };
    device->updateDescriptorSets(write, {});
}
}
