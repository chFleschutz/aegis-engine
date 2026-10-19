module aegis.rhi;
import std;
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

auto BindlessHeap::writeImageView(const Device& device, ImageView& view, ImageUsage usage,
    const ImageView* old) -> std::expected<void, Error>
{
    const bool sampled = utility::hasFlag(usage, ImageUsage::Sampled);
    const bool storage = utility::hasFlag(usage, ImageUsage::Storage);

    auto slots = detail::acquireImageSlots(m_freeSampledImages, m_freeStorageImages, sampled, storage,
        old ? old->sampledDescriptor() : std::nullopt,
        old ? old->storageDescriptor() : std::nullopt);
    if (!slots)
        return std::unexpected{ slots.error() };

    if (slots->sampled)
    {
        writeSampledImage(device, view, *slots->sampled);
        view.setSampledDescriptor(*slots->sampled);
    }
    if (slots->storage)
    {
        writeStorageImage(device, view, *slots->storage);
        view.setStorageDescriptor(*slots->storage);
    }
    return {};
}

auto BindlessHeap::writeSampler(const Device& device, vk::Sampler sampler)
    -> std::expected<SamplerDescriptor, Error>
{
    auto index = m_freeSamplers.pop();
    if (!index)
        return makeError(ErrorCode::OutOfDescriptors);

    SamplerDescriptor descriptor{ *index };
    updateDescriptorSet(device, DescriptorType::Sampler, descriptor.index, vk::DescriptorImageInfo{ .sampler = sampler });
    return descriptor;
}

auto BindlessHeap::writeSampledImage(const Device& device, const ImageView& view,
    SampledImageDescriptor descriptor) const -> void
{
    updateDescriptorSet(device,
        DescriptorType::SampledImage,
        descriptor.index,
        vk::DescriptorImageInfo{
            .imageView = view.vk(),
            .imageLayout = vk::ImageLayout::eReadOnlyOptimal });
}

auto BindlessHeap::writeStorageImage(const Device& device, const ImageView& view,
    StorageImageDescriptor descriptor) const -> void
{
    updateDescriptorSet(device,
        DescriptorType::StorageImage,
        descriptor.index,
        vk::DescriptorImageInfo{ .imageView = view.vk(), .imageLayout = vk::ImageLayout::eGeneral });
}

auto BindlessHeap::freeSampledImage(SampledImageDescriptor descriptor) -> void
{
    m_freeSampledImages.push(descriptor.index);
}

auto BindlessHeap::freeStorageImage(StorageImageDescriptor descriptor) -> void
{
    m_freeStorageImages.push(descriptor.index);
}

auto BindlessHeap::freeSampler(SamplerDescriptor descriptor) -> void
{
    m_freeSamplers.push(descriptor.index);
}

BindlessHeap::BindlessHeap(const Desc& desc, vk::raii::DescriptorPool&& pool,
    vk::raii::DescriptorSetLayout&& layout, vk::DescriptorSet set) :
    m_pool{ std::move(pool) },
    m_layout{ std::move(layout) },
    m_set{ set },
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
    flags.fill(vk::DescriptorBindingFlagBits::ePartiallyBound |
               vk::DescriptorBindingFlagBits::eUpdateAfterBind |
               vk::DescriptorBindingFlagBits::eUpdateUnusedWhilePending);

    vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
        .bindingCount = static_cast<std::uint32_t>(flags.size()),
        .pBindingFlags = flags.data(),
    };

    vk::DescriptorSetLayoutCreateInfo layoutInfo{
        .pNext = &flagsInfo,
        .flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
        .bindingCount = static_cast<std::uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };

    auto layout = device.createDescriptorSetLayout(layoutInfo);
    if (!layout.has_value())
        return makeError(toRHI(layout.error()));

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
        .poolSizeCount = static_cast<std::uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };

    auto pool = device.createDescriptorPool(poolInfo);
    if (!pool.has_value())
        return makeError(toRHI(pool.error()));

    return std::move(*pool);
}

auto BindlessHeap::createSet(const vk::raii::Device& device, vk::DescriptorPool pool,
    vk::DescriptorSetLayout layout) -> std::expected<vk::DescriptorSet, Error>
{
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &layout,
    };

    auto set = device.allocateDescriptorSets(allocInfo);
    if (!set.has_value() || set->empty())
        return makeError(toRHI(set.error()));

    return set->front().release();
}

auto BindlessHeap::updateDescriptorSet(const Device& device, DescriptorType type, std::uint32_t index,
    const vk::DescriptorImageInfo& info) const -> void
{
    vk::WriteDescriptorSet write{
        .dstSet = m_set,
        .dstBinding = binding(type),
        .dstArrayElement = index,
        .descriptorCount = 1,
        .descriptorType = toVulkan(type),
        .pImageInfo = &info,
    };
    device->updateDescriptorSets(write, {});
}
}
