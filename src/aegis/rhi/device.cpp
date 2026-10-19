module aegis.rhi;
import std;
import :device;
import :context;
import :debug;
import :error;
import :upload_manager;
import :vulkan_conversions;
import vulkan;

namespace aegis::rhi
{
namespace
{
auto toDeviceKind(vk::PhysicalDeviceType type) -> detail::DeviceKind
{
    switch (type)
    {
    case vk::PhysicalDeviceType::eDiscreteGpu: return detail::DeviceKind::Discrete;
    case vk::PhysicalDeviceType::eIntegratedGpu: return detail::DeviceKind::Integrated;
    case vk::PhysicalDeviceType::eVirtualGpu: return detail::DeviceKind::Virtual;
    case vk::PhysicalDeviceType::eCpu: return detail::DeviceKind::Cpu;
    case vk::PhysicalDeviceType::eOther: return detail::DeviceKind::Other;
    }
    return detail::DeviceKind::Other;
}
}

Device::Device(
    vk::raii::PhysicalDevice pd,
    vk::raii::Device device,
    Allocator allocator,
    Queue graphicsQueue,
    Queue computeQueue,
    Queue transferQueue,
    std::optional<Queue> presentQueue,
    Capabilities capabilities,
    Properties properties,
    BindlessHeap bindlessHeap,
    vk::raii::PipelineLayout pipelineLayout) :
    m_physicalDevice{ std::move(pd) },
    m_device{ std::move(device) },
    m_allocator{ std::move(allocator) },
    m_graphicsQueue{ std::move(graphicsQueue) },
    m_computeQueue{ std::move(computeQueue) },
    m_transferQueue{ std::move(transferQueue) },
    m_presentQueue{ std::move(presentQueue) },
    m_capabilities{ capabilities },
    m_properties{ properties },
    m_bindlessHeap{ std::move(bindlessHeap) },
    m_pipelineLayout{ std::move(pipelineLayout) }
{
}

Device::~Device()
{
    waitIdle();
}

auto Device::physicalDevice() const noexcept -> const vk::raii::PhysicalDevice&
{
    return m_physicalDevice;
}

auto Device::createBuffer(const Buffer::Desc& desc) -> std::expected<BufferHandle, Error>
{
    return Buffer::create(*this, desc)
        .transform([&](auto&& buffer) -> BufferHandle {
            return m_buffers.push(std::move(buffer));
        });
}

auto Device::createImage(const Image::Desc& desc) -> std::expected<ImageHandle, Error>
{
    return Image::create(*this, desc)
        .transform([&](auto&& image) -> ImageHandle {
            return m_images.push(std::move(image));
        });
}

auto Device::createImageView(ImageHandle image, const ImageView::Desc& desc)
    -> std::expected<ImageViewHandle, Error>
{
    return ImageView::create(*this, image, desc)
        .and_then([&](auto&& view) -> std::expected<ImageViewHandle, Error> {
            if (auto written = m_bindlessHeap.writeImageView(*this, view, desc.usage); !written)
                return std::unexpected{ written.error() };

            return m_imageViews.push(std::move(view));
        });
}

auto Device::createImageView(vk::Image imageSrc, const ImageView::Desc& desc)
    -> std::expected<ImageViewHandle, Error>
{
    return ImageView::create(*this, imageSrc, desc)
        .and_then([&](auto&& view) -> std::expected<ImageViewHandle, Error> {
            if (auto written = m_bindlessHeap.writeImageView(*this, view, desc.usage); !written)
                return std::unexpected{ written.error() };

            return m_imageViews.push(std::move(view));
        });
}

auto Device::createSampler(const Sampler::Desc& desc) -> std::expected<SamplerHandle, Error>
{
    return Sampler::create(*this, desc)
        .and_then([&](auto&& sampler) -> std::expected<SamplerHandle, Error> {
            auto descriptor = m_bindlessHeap.writeSampler(*this, sampler.vk());
            if (!descriptor)
                return std::unexpected{ descriptor.error() };
            sampler.m_descriptor = *descriptor;

            return m_samplers.push(std::move(sampler));
        });
}

auto Device::replace(BufferHandle handle, const Buffer::Desc& desc)
    -> std::expected<BufferHandle, Error>
{
    return Buffer::create(*this, desc)
        .transform([&](auto&& buffer) {
            auto oldBuffer = m_buffers.replace(handle, std::move(buffer));
            m_deletionQueue.push(retireValue(), std::move(oldBuffer));
            return handle;
        });
}

auto Device::replace(ImageHandle handle, const Image::Desc& desc)
    -> std::expected<ImageHandle, Error>
{
    return Image::create(*this, desc)
        .transform([&](auto&& image) {
            auto oldImage = m_images.replace(handle, std::move(image));
            m_deletionQueue.push(retireValue(), std::move(oldImage));
            return handle;
        });
}

auto Device::replace(ImageViewHandle view, ImageHandle image, const ImageView::Desc& desc)
    -> std::expected<ImageViewHandle, Error>
{
    return ImageView::create(*this, image, desc)
        .and_then([&](auto&& imageView) -> std::expected<ImageViewHandle, Error> {
            auto& oldView = m_imageViews.get(view);
            if (auto written = m_bindlessHeap.writeImageView(*this, imageView, desc.usage, &oldView); !written)
                return std::unexpected{ written.error() };

            // A usage bit the new view drops leaves its old slot unused: return it once the GPU is done.
            const auto droppedSampled = imageView.sampledDescriptor() ? std::nullopt : oldView.sampledDescriptor();
            const auto droppedStorage = imageView.storageDescriptor() ? std::nullopt : oldView.storageDescriptor();
            if (droppedSampled || droppedStorage)
            {
                m_deletionQueue.defer(retireValue(), [this, droppedSampled, droppedStorage] {
                    if (droppedSampled)
                        m_bindlessHeap.freeSampledImage(*droppedSampled);
                    if (droppedStorage)
                        m_bindlessHeap.freeStorageImage(*droppedStorage);
                });
            }

            m_deletionQueue.push(retireValue(), m_imageViews.replace(view, std::move(imageView)));
            return view;
        });
}

auto Device::free(BufferHandle handle) -> void
{
    m_deletionQueue.push(retireValue(), m_buffers.pop(handle));
}

auto Device::free(ImageHandle handle) -> void
{
    m_deletionQueue.push(retireValue(), m_images.pop(handle));
}

auto Device::free(ImageViewHandle handle) -> void
{
    // The slots go back to the heap only once the GPU is done, or a new resource could overwrite a
    // descriptor that an in-flight frame still reads.
    const auto& view = m_imageViews.get(handle);
    m_deletionQueue.defer(retireValue(),
        [this, sampled = view.sampledDescriptor(), storage = view.storageDescriptor()] {
            if (sampled)
                m_bindlessHeap.freeSampledImage(*sampled);
            if (storage)
                m_bindlessHeap.freeStorageImage(*storage);
        });
    m_deletionQueue.push(retireValue(), m_imageViews.pop(handle));
}

auto Device::free(SamplerHandle handle) -> void
{
    m_deletionQueue.defer(retireValue(),
        [this, descriptor = m_samplers.get(handle).descriptor()] {
            m_bindlessHeap.freeSampler(descriptor);
        });
    m_deletionQueue.push(retireValue(), m_samplers.pop(handle));
}

auto Device::supportsMipGeneration(Format format) const -> bool
{
    // Depth/stencil blits must use nearest filtering, so linear mip generation never applies.
    if (deriveImageAspectFlags(format) != vk::ImageAspectFlagBits::eColor)
        return false;

    constexpr auto required = vk::FormatFeatureFlagBits::eBlitSrc | vk::FormatFeatureFlagBits::eBlitDst |
                              vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
    const auto features = m_physicalDevice.getFormatProperties(toVulkan(format)).optimalTilingFeatures;
    return (features & required) == required;
}

auto Device::retireValue() const noexcept -> TimelineValue
{
    return m_graphicsQueue.lastSubmitted() + 1;
}

auto Device::setFrameCompleted(TimelineValue frame) -> void
{
    m_deletionQueue.collect(frame);
}

auto Device::upload(BufferHandle dst, std::span<const std::byte> data, std::size_t alignment,
    std::size_t dstOffset) -> std::expected<void, Error>
{
    return m_uploadManager->upload(get(dst), data, alignment, dstOffset);
}

auto Device::upload(ImageHandle dst, std::span<const std::byte> data, bool generateMips,
    std::optional<ResourceState> finalState) -> std::expected<void, Error>
{
    return m_uploadManager->upload(dst, data, generateMips, finalState);
}

auto Device::flushUploads() -> std::optional<TimelineValue>
{
    return m_uploadManager->flushPending(m_graphicsQueue);
}

auto Device::createCommandBuffer(const CommandBuffer::Desc& desc) const -> std::expected<CommandBuffer, Error>
{
    return CommandBuffer::create(*this, desc);
}

auto Device::createCommandPool(const CommandPool::Desc& desc) const -> std::expected<CommandPool, Error>
{
    return CommandPool::create(*this, desc);
}

auto Device::createFence(const Fence::Desc& desc) const -> std::expected<Fence, Error>
{
    return Fence::create(*this, desc);
}

auto Device::createPipeline(const Pipeline::GraphicsDesc& desc) const -> std::expected<Pipeline, Error>
{
    return Pipeline::create(*this, desc);
}

auto Device::createQueryPool(const QueryPool::Desc& desc) const -> std::expected<QueryPool, Error>
{
    return QueryPool::create(*this, desc, m_graphicsQueue.family());
}

auto Device::createPipeline(const Pipeline::ComputeDesc& desc) const -> std::expected<Pipeline, Error>
{
    return Pipeline::create(*this, desc);
}

auto Device::createSemaphore(const Semaphore::Desc& desc) const -> std::expected<Semaphore, Error>
{
    return Semaphore::create(m_device, desc);
}

auto Device::createSwapchain(const Swapchain::Desc& desc) -> std::expected<Swapchain, Error>
{
    return Swapchain::create(*this, desc);
}

auto Device::createSwapchain(const Swapchain::RecreateDesc& desc) -> std::expected<Swapchain, Error>
{
    // Create first: if it fails, the old swapchain stays whole and its views are not freed (twice, on a retry).
    auto swapchain = Swapchain::create(*this, desc);
    if (!swapchain)
        return swapchain;

    for (const auto imageView : desc.oldSwapchain.imageViews())
    {
        free(imageView);
    }
    return swapchain;
}

auto Device::waitIdle() const noexcept -> void
{
    std::ignore = m_device.waitIdle();
}

namespace detail
{
auto QueueFamilySelection::isComplete(bool presentation) const -> bool
{
    return graphics != noQueueFamily && (!presentation || present != noQueueFamily) &&
           compute != noQueueFamily && transfer != noQueueFamily;
}

auto rankPhysicalDevice(const DeviceCandidate& candidate) -> std::optional<std::uint64_t>
{
    if (!candidate.queuesComplete || !candidate.extensionsSupported || candidate.apiVersion < minApiVersion)
        return std::nullopt;

    // Better kinds have lower enum values; invert so the kind outweighs any image limit.
    const auto kindScore = static_cast<std::uint64_t>(DeviceKind::Other) - static_cast<std::uint64_t>(candidate.kind);
    return (kindScore << 32) | candidate.maxImageDimension2D;
}

auto matchesDeviceName(std::string_view deviceName, std::string_view filter) -> bool
{
    if (filter.empty())
        return true;
    const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return !std::ranges::search(deviceName, filter, {}, lower, lower).empty();
}

auto pickQueueFamilies(std::span<const QueueFamilyInfo> families, bool presentation) -> QueueFamilySelection
{
    QueueFamilySelection selection;
    for (std::uint32_t i = 0; i < families.size(); ++i)
    {
        const auto& family = families[i];

        // Graphics queue (required)
        if (family.graphics && selection.graphics == noQueueFamily)
            selection.graphics = i;

        // Dedicated compute queue
        if (family.compute && !family.graphics && selection.compute == noQueueFamily)
            selection.compute = i;

        // Dedicated transfer queue
        if (family.transfer && !family.graphics && !family.compute && selection.transfer == noQueueFamily)
            selection.transfer = i;

        // Present queue: prefer the graphics family. Headless has no surface to present to.
        if (presentation && family.present && (selection.present == noQueueFamily || i == selection.graphics))
            selection.present = i;
    }

    // Fallback: graphics for compute/transfer if no dedicated family exists.
    if (selection.compute == noQueueFamily)
        selection.compute = selection.graphics;
    if (selection.transfer == noQueueFamily)
        selection.transfer = selection.graphics;

    // No fallback for present, so the selection stays incomplete without a present-capable family.
    return selection;
}
}

auto Device::create(const vk::raii::Instance& instance, const vk::raii::SurfaceKHR& surface, const Desc& desc)
    -> std::expected<std::unique_ptr<Device>, Error>
{
    auto physicalDevice = createPhysicalDevice(instance, surface, desc.deviceName);
    if (!physicalDevice)
        return std::unexpected{ physicalDevice.error() };

    const bool presentation = *surface != nullptr;
    auto queueFamilies = queryQueueFamilies(*physicalDevice, surface);
    if (!queueFamilies.isComplete(presentation))
        return makeError(ErrorCode::InitializationFailed);

    auto capabilities = queryCapabilities(*physicalDevice);
    auto properties = queryProperties(*physicalDevice);

    auto device = createDevice(*physicalDevice, capabilities, queueFamilies, presentation);
    if (!device)
        return std::unexpected{ device.error() };

    auto allocator = Allocator::create(instance, *device, *physicalDevice);
    if (!allocator)
        return std::unexpected{ allocator.error() };

    std::optional<Queue> presentQueue;
    if (presentation)
    {
        auto queue = createQueue(*device, "PresentQueue", queueFamilies.present);
        if (!queue)
            return std::unexpected{ queue.error() };
        presentQueue.emplace(std::move(*queue));
    }

    auto transferQueue = createQueue(*device, "TransferQueue", queueFamilies.transfer);
    if (!transferQueue)
        return std::unexpected{ transferQueue.error() };

    auto computeQueue = createQueue(*device, "ComputeQueue", queueFamilies.compute);
    if (!computeQueue)
        return std::unexpected{ computeQueue.error() };

    auto graphicsQueue = createQueue(*device, "GraphicsQueue", queueFamilies.graphics);
    if (!graphicsQueue)
        return std::unexpected{ graphicsQueue.error() };

    // Desc's defaults, clamped to what the device allows for update-after-bind descriptors.
    constexpr BindlessHeap::Desc wanted{};
    const auto& limits = properties.vk12;
    auto bindless = BindlessHeap::create(*device,
        BindlessHeap::Desc{
            .maxSampledImages = std::min({ wanted.maxSampledImages,
                limits.maxPerStageDescriptorUpdateAfterBindSampledImages,
                limits.maxDescriptorSetUpdateAfterBindSampledImages }),
            .maxStorageImages = std::min({ wanted.maxStorageImages,
                limits.maxPerStageDescriptorUpdateAfterBindStorageImages,
                limits.maxDescriptorSetUpdateAfterBindStorageImages }),
            .maxSamplers = std::min({ wanted.maxSamplers,
                limits.maxPerStageDescriptorUpdateAfterBindSamplers,
                limits.maxDescriptorSetUpdateAfterBindSamplers }),
        });
    if (!bindless)
        return std::unexpected{ bindless.error() };

    auto pipelineLayout = createPipelineLayout(*device, *bindless);
    if (!pipelineLayout)
        return std::unexpected{ pipelineLayout.error() };

    auto result = std::make_unique<Device>(
        std::move(*physicalDevice),
        std::move(*device),
        std::move(*allocator),
        std::move(*graphicsQueue),
        std::move(*computeQueue),
        std::move(*transferQueue),
        std::move(presentQueue),
        capabilities,
        properties,
        std::move(*bindless),
        std::move(*pipelineLayout));

    // Created after the Device exists because UploadManager::create calls back into
    // device.createBuffer / createCommandPool. Flushes on the graphics queue for now.
    auto uploadManager = UploadManager::create(*result, result->graphicsQueue().family(), {});
    if (!uploadManager)
        return std::unexpected{ uploadManager.error() };
    result->m_uploadManager.emplace(std::move(*uploadManager));

    return result;
}

auto Device::createPhysicalDevice(const vk::raii::Instance& instance, const vk::raii::SurfaceKHR& surface,
    std::string_view deviceName)
    -> std::expected<vk::raii::PhysicalDevice, Error>
{
    auto enumerated = instance.enumeratePhysicalDevices();
    if (!enumerated.has_value())
        return makeError(toRHI(enumerated.error()));

    auto& physicalDevices = *enumerated;

    if (physicalDevices.empty())
        return makeError(ErrorCode::Unknown);

    const bool presentation = *surface != nullptr;

    // Ties go to the earlier device, so enumeration order is only the last resort.
    std::optional<std::size_t> best;
    std::uint64_t bestScore{ 0 };
    for (std::size_t i = 0; i < physicalDevices.size(); ++i)
    {
        const auto& physicalDevice = physicalDevices[i];
        const auto properties = physicalDevice.getProperties();
        if (!detail::matchesDeviceName(properties.deviceName.data(), deviceName))
            continue;

        // TODO: handle missing capabilities
        // auto capabilities = queryCapabilities(physicalDevice);

        const auto score = detail::rankPhysicalDevice({
            .kind = toDeviceKind(properties.deviceType),
            .apiVersion = properties.apiVersion,
            .maxImageDimension2D = properties.limits.maxImageDimension2D,
            .queuesComplete = queryQueueFamilies(physicalDevice, surface).isComplete(presentation),
            .extensionsSupported = supportsExtensions(physicalDevice, presentation),
        });
        if (score && (!best || *score > bestScore))
        {
            best = i;
            bestScore = *score;
        }
    }

    if (!best)
        return makeError(ErrorCode::Unknown);
    return physicalDevices[*best];
}

auto Device::queryQueueFamilies(const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::SurfaceKHR& surface)
    -> QueueFamilyIndices
{
    const auto properties = physicalDevice.getQueueFamilyProperties();
    std::vector<detail::QueueFamilyInfo> families;
    families.reserve(properties.size());
    for (std::uint32_t i = 0; i < properties.size(); ++i)
    {
        const auto flags = properties[i].queueFlags;
        bool canPresent{ false };
        if (*surface)
        {
            auto support = physicalDevice.getSurfaceSupportKHR(i, *surface);
            canPresent = support.has_value() && *support;
        }
        families.push_back({
            .graphics = static_cast<bool>(flags & vk::QueueFlagBits::eGraphics),
            .compute = static_cast<bool>(flags & vk::QueueFlagBits::eCompute),
            .transfer = static_cast<bool>(flags & vk::QueueFlagBits::eTransfer),
            .present = canPresent,
        });
    }
    return detail::pickQueueFamilies(families, *surface != nullptr);
}

auto Device::createQueue(const vk::raii::Device& device, std::string_view name, std::uint32_t queueFamily)
    -> std::expected<Queue, Error>
{
    auto queue = device.getQueue(queueFamily, 0);
    debug::setName(device, *queue, name);

    auto semaphore = Semaphore::create(device,
        Semaphore::Desc{
            .name = std::format("{}TimelineSemaphore", name),
            .type = Semaphore::Type::Timeline,
        });
    if (!semaphore)
        return std::unexpected{ semaphore.error() };

    return Queue{ std::move(queue), std::move(*semaphore), queueFamily };
}

auto Device::createDevice(const vk::raii::PhysicalDevice& pd, const Capabilities& capabilities,
    const QueueFamilyIndices& queueFamilyIndices, bool presentation)
    -> std::expected<vk::raii::Device, Error>
{
    auto features = createFeatureChain(capabilities);

    std::set uniqueQueueFamilies{
        queueFamilyIndices.graphics,
        queueFamilyIndices.compute,
        queueFamilyIndices.transfer,
    };
    if (presentation)
        uniqueQueueFamilies.insert(queueFamilyIndices.present);

    auto queuePriority = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    for (std::uint32_t family : uniqueQueueFamilies)
    {
        queueCreateInfos.emplace_back(
            vk::DeviceQueueCreateInfo{
                .queueFamilyIndex = family,
                .queueCount = 1,
                .pQueuePriorities = &queuePriority,
            });
    }

    auto extensions = queryExtensions(capabilities, presentation);

    auto deviceInfo = vk::DeviceCreateInfo{}
                          .setPNext(&features.get<vk::PhysicalDeviceFeatures2>())
                          .setQueueCreateInfos(queueCreateInfos)
                          .setPEnabledExtensionNames(extensions);

    auto device = pd.createDevice(deviceInfo);
    if (!device.has_value())
        return makeError(toRHI(device.error()));

    return std::move(*device);
}

auto Device::queryCapabilities(const vk::raii::PhysicalDevice& pd) -> Capabilities
{
    auto features = pd.getFeatures2< //
        vk::PhysicalDeviceFeatures2,
        vk::PhysicalDeviceVulkan11Features,
        vk::PhysicalDeviceVulkan12Features,
        vk::PhysicalDeviceVulkan13Features,
        vk::PhysicalDeviceMeshShaderFeaturesEXT>();
    // const auto& vk10features = features.get<vk::PhysicalDeviceFeatures2>();
    // const auto& vk11features = features.get<vk::PhysicalDeviceVulkan11Features>();
    // const auto& vk12features = features.get<vk::PhysicalDeviceVulkan12Features>();
    // const auto& vk13features = features.get<vk::PhysicalDeviceVulkan13Features>();
    const auto& meshShaderFeatures = features.get<vk::PhysicalDeviceMeshShaderFeaturesEXT>();

    return Capabilities{
        .meshShaders = meshShaderFeatures.meshShader && meshShaderFeatures.taskShader,
    };
}

auto Device::requiredExtensions(bool presentation) -> std::vector<const char*>
{
    std::vector<const char*> extensions;
    if (presentation)
        extensions.assign(presentationExtensions.begin(), presentationExtensions.end());
    return extensions;
}

auto Device::queryExtensions(const Capabilities& caps, bool presentation) -> std::vector<const char*>
{
    auto extensions = requiredExtensions(presentation);

    if (caps.meshShaders)
    {
        extensions.emplace_back(vk::EXTMeshShaderExtensionName);
    }

    return extensions;
}

auto Device::supportsExtensions(const vk::raii::PhysicalDevice& pd, bool presentation) -> bool
{
    auto availableExtensions = pd.enumerateDeviceExtensionProperties();
    if (!availableExtensions.has_value())
        return false;

    return std::ranges::all_of(requiredExtensions(presentation),
        [&availableExtensions](const auto& required) {
            return std::ranges::any_of(*availableExtensions,
                [&required](const auto& available) {
                    return std::string_view{ available.extensionName } == std::string_view{ required };
                });
        });
}

auto Device::createFeatureChain(const Capabilities& capabilities) -> FeatureChain
{
    // TODO: Only enable supported features

    FeatureChain featureChain;

    // Storage images are read and written without a format: the bindless heap's storage binding
    // holds images of any format. Shaders hold some device addresses as 64-bit integers
    // (shaders/modules/common.slang's Mesh).
    featureChain.get<vk::PhysicalDeviceFeatures2>()
        .features.setSamplerAnisotropy(true)
        .setShaderStorageImageReadWithoutFormat(true)
        .setShaderStorageImageWriteWithoutFormat(true)
        .setShaderInt64(true);

    featureChain
        .get<vk::PhysicalDeviceVulkan11Features>() //
        .setShaderDrawParameters(true);

    featureChain.get<vk::PhysicalDeviceVulkan12Features>()
        .setStorageBuffer8BitAccess(true)
        .setUniformAndStorageBuffer8BitAccess(true)
        .setStoragePushConstant8(true)
        .setShaderInt8(true)
        .setDescriptorIndexing(true)
        .setShaderUniformBufferArrayNonUniformIndexing(true)
        .setShaderSampledImageArrayNonUniformIndexing(true)
        .setShaderStorageBufferArrayNonUniformIndexing(true)
        .setShaderStorageImageArrayNonUniformIndexing(true)
        .setDescriptorBindingUniformBufferUpdateAfterBind(true)
        .setDescriptorBindingSampledImageUpdateAfterBind(true)
        .setDescriptorBindingStorageImageUpdateAfterBind(true)
        .setDescriptorBindingStorageBufferUpdateAfterBind(true)
        .setDescriptorBindingUpdateUnusedWhilePending(true)
        .setDescriptorBindingPartiallyBound(true)
        .setDescriptorBindingVariableDescriptorCount(true)
        .setRuntimeDescriptorArray(true)
        .setScalarBlockLayout(true)
        .setUniformBufferStandardLayout(true)
        .setTimelineSemaphore(true)
        .setDrawIndirectCount(true)
        .setBufferDeviceAddress(true);

    featureChain.get<vk::PhysicalDeviceVulkan13Features>()
        .setShaderDemoteToHelperInvocation(true)
        .setSynchronization2(true)
        .setDynamicRendering(true)
        .setMaintenance4(true);

    // Only chained (and its extension only enabled) where the device has mesh shaders.
    if (capabilities.meshShaders)
    {
        featureChain
            .get<vk::PhysicalDeviceMeshShaderFeaturesEXT>() //
            .setMeshShader(true)
            .setTaskShader(true);
    }
    else
    {
        featureChain.unlink<vk::PhysicalDeviceMeshShaderFeaturesEXT>();
    }

    return featureChain;
}

auto Device::createPipelineLayout(const vk::raii::Device& device, const BindlessHeap& bindlessHeap)
    -> std::expected<vk::raii::PipelineLayout, Error>
{
    // 128 bytes is the minimum maxPushConstantsSize every device guarantees. One range for all stages
    // keeps every pipeline layout-compatible, so the bindless set survives pipeline changes.
    const auto setLayout = bindlessHeap.layout();
    const vk::PushConstantRange pushConstants{
        .stageFlags = vk::ShaderStageFlagBits::eAll,
        .offset = 0,
        .size = CommandBuffer::maxPushConstantSize,
    };

    auto layout = device.createPipelineLayout(vk::PipelineLayoutCreateInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &setLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstants,
    });
    if (!layout.has_value())
        return makeError(toRHI(layout.error()));

    debug::setName(device, **layout, "SharedPipelineLayout");
    return std::move(*layout);
}

auto Device::queryProperties(const vk::raii::PhysicalDevice& pd) -> Properties
{
    auto props = pd.getProperties2<
        vk::PhysicalDeviceProperties2,
        vk::PhysicalDeviceVulkan11Properties,
        vk::PhysicalDeviceVulkan12Properties,
        vk::PhysicalDeviceVulkan13Properties>();

    return Properties{
        .core = props.get<vk::PhysicalDeviceProperties2>(),
        .vk11 = props.get<vk::PhysicalDeviceVulkan11Properties>(),
        .vk12 = props.get<vk::PhysicalDeviceVulkan12Properties>(),
        .vk13 = props.get<vk::PhysicalDeviceVulkan13Properties>(),
    };
}
}
