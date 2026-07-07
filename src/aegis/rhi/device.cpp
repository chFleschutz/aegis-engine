module;
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <vector>

module aegis.rhi;
import :device;
import :context;
import :debug;
import :error;
import :upload_manager;
import :utility;
import :vulkan_conversions;
import vulkan_hpp;

namespace aegis::rhi
{
Device::Device(
    vk::raii::PhysicalDevice pd,
    vk::raii::Device device,
    Allocator allocator,
    Queue graphicsQueue,
    Queue computeQueue,
    Queue transferQueue,
    Queue presentQueue,
    Capabilities capabilities,
    Properties properties,
    BindlessHeap bindlessHeap) :
    m_physicalDevice{ std::move(pd) },
    m_device{ std::move(device) },
    m_allocator{ std::move(allocator) },
    m_graphicsQueue{ std::move(graphicsQueue) },
    m_computeQueue{ std::move(computeQueue) },
    m_transferQueue{ std::move(transferQueue) },
    m_presentQueue{ std::move(presentQueue) },
    m_capabilities{ capabilities },
    m_properties{ properties },
    m_bindlessHeap{ std::move(bindlessHeap) }
{
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
            if (utility::hasFlag(desc.usage, ImageUsage::Sampled))
            {
                auto handle = m_bindlessHeap.writeSampledImage(*this, view);
                if (!handle)
                    return makeError(ErrorCode::InitializationFailed);
                view.setSampledHandle(*handle);
            }

            if (utility::hasFlag(desc.usage, ImageUsage::Storage))
            {
                auto handle = m_bindlessHeap.writeStorageImage(*this, view);
                if (!handle)
                    return makeError(ErrorCode::InitializationFailed);
                view.setStorageHandle(*handle);
            }

            return m_imageViews.push(std::move(view));
        });
}

auto Device::createImageView(vk::Image imageSrc, const ImageView::Desc& desc)
    -> std::expected<ImageViewHandle, Error>
{
    return ImageView::create(*this, imageSrc, desc)
        .and_then([&](auto&& view) -> std::expected<ImageViewHandle, Error> {
            if (utility::hasFlag(desc.usage, ImageUsage::Sampled))
            {
                auto handle = m_bindlessHeap.writeSampledImage(*this, view);
                if (!handle)
                    return makeError(ErrorCode::InitializationFailed);
                view.setSampledHandle(*handle);
            }

            if (utility::hasFlag(desc.usage, ImageUsage::Storage))
            {
                auto handle = m_bindlessHeap.writeStorageImage(*this, view);
                if (!handle)
                    return makeError(ErrorCode::InitializationFailed);
                view.setStorageHandle(*handle);
            }

            return m_imageViews.push(std::move(view));
        });
}

auto Device::replace(BufferHandle handle, const Buffer::Desc& desc)
    -> std::expected<BufferHandle, Error>
{
    return Buffer::create(*this, desc)
        .transform([&](auto&& buffer) {
            auto oldBuffer = m_buffers.replace(handle, std::move(buffer));
            m_deletionQueue.push(m_currentValue, std::move(oldBuffer));
            return handle;
        });
}

auto Device::replace(ImageHandle handle, const Image::Desc& desc)
    -> std::expected<ImageHandle, Error>
{
    return Image::create(*this, desc)
        .transform([&](auto&& image) {
            auto oldImage = m_images.replace(handle, std::move(image));
            m_deletionQueue.push(m_currentValue, std::move(oldImage));
            return handle;
        });
}

auto Device::replace(ImageViewHandle view, ImageHandle image, const ImageView::Desc& desc)
    -> std::expected<ImageViewHandle, Error>
{
    return ImageView::create(*this, image, desc)
        .and_then([&](auto&& imageView) -> std::expected<ImageViewHandle, Error> {
            auto& oldView = m_imageViews.get(view);
            if (utility::hasFlag(desc.usage, ImageUsage::Sampled))
            {
                auto handle = m_bindlessHeap.writeSampledImage(*this, imageView, oldView.sampledHandle());
                if (!handle)
                    return makeError(ErrorCode::InitializationFailed);
                imageView.setSampledHandle(*handle);
            }

            if (utility::hasFlag(desc.usage, ImageUsage::Storage))
            {
                auto handle = m_bindlessHeap.writeStorageImage(*this, imageView, oldView.storageHandle());
                if (!handle)
                    return makeError(ErrorCode::InitializationFailed);
                imageView.setStorageHandle(*handle);
            }

            m_deletionQueue.push(m_currentValue, m_imageViews.replace(view, std::move(imageView)));
            return view;
        });
}

auto Device::free(BufferHandle handle) -> void
{
    m_deletionQueue.push(m_currentValue, m_buffers.pop(handle));
}

auto Device::free(ImageHandle handle) -> void
{
    m_deletionQueue.push(m_currentValue, m_images.pop(handle));
}

auto Device::free(ImageViewHandle handle) -> void
{
    if (auto h = m_imageViews.get(handle).sampledHandle())
        m_bindlessHeap.freeSampledImage(*h);
    if (auto h = m_imageViews.get(handle).storageHandle())
        m_bindlessHeap.freeStorageImage(*h);
    m_deletionQueue.push(m_currentValue, m_imageViews.pop(handle));
}

auto Device::setFrameCompleted(TimelineValue frame) -> void
{
    m_currentValue = frame;
    m_deletionQueue.collect(frame);
}

auto Device::upload(BufferHandle dst, std::span<const std::byte> data, std::size_t alignment,
    std::size_t dstOffset) -> bool
{
    return m_uploadManager->upload(get(dst), data, alignment, dstOffset);
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
    for (const auto imageView : desc.oldSwapchain.imageViews())
    {
        free(imageView);
    }
    return Swapchain::create(*this, desc);
}

auto Device::waitIdle() const noexcept -> void
{
    std::ignore = m_device.waitIdle();
}

auto Device::QueueFamilyIndices::isComplete() const -> bool
{
    return graphics != vk::QueueFamilyIgnored && present != vk::QueueFamilyIgnored &&
           compute != vk::QueueFamilyIgnored && transfer != vk::QueueFamilyIgnored;
}

auto Device::create(const vk::raii::Instance& instance, const vk::raii::SurfaceKHR& surface, const Desc& desc)
    -> std::expected<std::unique_ptr<Device>, Error>
{
    auto physicalDevice = createPhysicalDevice(instance, surface);
    if (!physicalDevice)
        return std::unexpected{ physicalDevice.error() };

    auto queueFamilies = queryQueueFamilies(*physicalDevice, surface);
    auto capabilities = queryCapabilities(*physicalDevice);
    auto properties = queryProperties(*physicalDevice);

    auto device = createDevice(*physicalDevice, capabilities, queueFamilies);
    if (!device)
        return std::unexpected{ device.error() };

    auto allocator = Allocator::create(instance, *device, *physicalDevice);
    if (!allocator)
        return std::unexpected{ allocator.error() };

    auto presentQueue = createQueue(*device, "PresentQueue", queueFamilies.present);
    if (!presentQueue)
        return std::unexpected{ presentQueue.error() };

    auto transferQueue = createQueue(*device, "TransferQueue", queueFamilies.transfer);
    if (!transferQueue)
        return std::unexpected{ transferQueue.error() };

    auto computeQueue = createQueue(*device, "ComputeQueue", queueFamilies.compute);
    if (!computeQueue)
        return std::unexpected{ computeQueue.error() };

    auto graphicsQueue = createQueue(*device, "GraphicsQueue", queueFamilies.graphics);
    if (!graphicsQueue)
        return std::unexpected{ graphicsQueue.error() };

    // TODO: query pd for max values
    auto bindless = BindlessHeap::create(*device,
        BindlessHeap::Desc{
            .maxSampledImages = 1024,
            .maxStorageImages = 1024,
            .maxSamplers = 1024,
        });
    if (!bindless)
        return std::unexpected{ bindless.error() };

    auto result = std::make_unique<Device>(
        std::move(*physicalDevice),
        std::move(*device),
        std::move(*allocator),
        std::move(*graphicsQueue),
        std::move(*computeQueue),
        std::move(*transferQueue),
        std::move(*presentQueue),
        capabilities,
        properties,
        std::move(*bindless));

    // Created after the Device exists because UploadManager::create calls back into
    // device.createBuffer / createCommandPool. Flushes on the graphics queue for now.
    auto uploadManager = UploadManager::create(*result, result->graphicsQueue().family(), {});
    if (!uploadManager)
        return std::unexpected{ uploadManager.error() };
    result->m_uploadManager.emplace(std::move(*uploadManager));

    return result;
}

auto Device::createPhysicalDevice(const vk::raii::Instance& instance, const vk::raii::SurfaceKHR& surface)
    -> std::expected<vk::raii::PhysicalDevice, Error>
{
    auto [result, physicalDevices] = instance.enumeratePhysicalDevices();
    if (result != vk::Result::eSuccess)
        return makeError(toRHI(result));

    if (physicalDevices.empty())
        return makeError(ErrorCode::Unknown);

    std::vector<std::pair<uint32_t, uint32_t>> candidates;
    for (size_t i = 0; i < physicalDevices.size(); ++i)
    {
        const auto& physicalDevice = physicalDevices[i];
        uint32_t score{ 0 };

        if (!queryQueueFamilies(physicalDevice, surface).isComplete())
            continue;

        // TODO: handle missing capabilities
        // auto capabilities = queryCapabilities(physicalDevice);

        if (!supportsExtensions(physicalDevice))
            continue;

        const auto properties = physicalDevice.getProperties();
        if (properties.apiVersion < vk::ApiVersion13)
            continue;

        if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
            score += 1000;

        score += properties.limits.maxImageDimension2D;
        candidates.emplace_back(score, i);
    }

    std::ranges::sort(candidates); // Sorts highest score at the end
    if (candidates.empty() || candidates.back().first == 0)
        return makeError(ErrorCode::Unknown);

    const auto& [_, index] = candidates.back();
    return physicalDevices[index];
}

auto Device::queryQueueFamilies(const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::SurfaceKHR& surface)
    -> QueueFamilyIndices
{
    QueueFamilyIndices indices;

    auto queueFamilies = physicalDevice.getQueueFamilyProperties();
    for (uint32_t i = 0; i < queueFamilies.size(); ++i)
    {
        const auto& props = queueFamilies[i];
        auto hasGraphics = static_cast<bool>(props.queueFlags & vk::QueueFlagBits::eGraphics);
        auto hasCompute = static_cast<bool>(props.queueFlags & vk::QueueFlagBits::eCompute);
        auto hasTransfer = static_cast<bool>(props.queueFlags & vk::QueueFlagBits::eTransfer);

        // Graphics queue (required)
        if (hasGraphics && indices.graphics == vk::QueueFamilyIgnored)
            indices.graphics = i;

        // Dedicated Compute Queue
        if (hasCompute && !hasGraphics && indices.compute == vk::QueueFamilyIgnored)
            indices.compute = i;

        // Dedicated Transfer Queue
        if (hasTransfer && !hasGraphics && !hasCompute && indices.transfer == vk::QueueFamilyIgnored)
            indices.transfer = i;

        // Present queue - prefer one that matches graphics
        auto [result, hasPresent] = physicalDevice.getSurfaceSupportKHR(i, *surface);
        if (result == vk::Result::eSuccess && hasPresent)
        {
            if (indices.present == vk::QueueFamilyIgnored || i == indices.graphics)
                indices.present = i;
        }
    }

    // Fallback: use graphics queue for compute/transfer if no dedicated queues found
    if (indices.compute == vk::QueueFamilyIgnored)
        indices.compute = indices.graphics;
    if (indices.transfer == vk::QueueFamilyIgnored)
        indices.transfer = indices.graphics;

    // Fallback: if no presentation queue matches graphics, use graphics queue
    if (indices.present == vk::QueueFamilyIgnored)
        indices.present = indices.graphics;

    assert(indices.isComplete() && "Queue family indices must be complete");
    return indices;
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
    const QueueFamilyIndices& queueFamilyIndices)
    -> std::expected<vk::raii::Device, Error>
{
    auto features = createFeatureChain();

    std::set uniqueQueueFamilies{
        queueFamilyIndices.graphics,
        queueFamilyIndices.compute,
        queueFamilyIndices.transfer,
        queueFamilyIndices.present,
    };

    auto queuePriority = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    for (uint32_t family : uniqueQueueFamilies)
    {
        queueCreateInfos.emplace_back(
            vk::DeviceQueueCreateInfo{
                .queueFamilyIndex = family,
                .queueCount = 1,
                .pQueuePriorities = &queuePriority,
            });
    }

    auto extensions = queryExtensions(capabilities);

    auto deviceInfo = vk::DeviceCreateInfo{}
        .setPNext(&features.get<vk::PhysicalDeviceFeatures2>())
        .setQueueCreateInfos(queueCreateInfos)
        .setPEnabledExtensionNames(extensions);

    auto device = pd.createDevice(deviceInfo);
    if (!device.has_value())
        return makeError(toRHI(device.result));

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

auto Device::queryExtensions(const Capabilities& caps) -> std::vector<const char*>
{
    std::vector extensions(requiredExtensions.begin(), requiredExtensions.end());

    if (caps.meshShaders)
    {
        extensions.emplace_back(vk::EXTMeshShaderExtensionName);
    }

    return extensions;
}

auto Device::supportsExtensions(const vk::raii::PhysicalDevice& pd) -> bool
{
    auto availableExtensions = pd.enumerateDeviceExtensionProperties();
    if (!availableExtensions.has_value())
        return false;

    return std::ranges::all_of(requiredExtensions,
        [&availableExtensions](const auto& required) {
            return std::ranges::any_of(*availableExtensions,
                [&required](const auto& available) {
                    return std::string_view{ available.extensionName } == std::string_view{ required };
                });
        });
}

auto Device::createFeatureChain() -> FeatureChain
{
    // TODO: Only enable supported features

    FeatureChain featureChain;

    featureChain.get<vk::PhysicalDeviceFeatures2>().features.setSamplerAnisotropy(true);

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
        .setBufferDeviceAddress(true);

    featureChain.get<vk::PhysicalDeviceVulkan13Features>()
        .setShaderDemoteToHelperInvocation(true)
        .setSynchronization2(true)
        .setDynamicRendering(true)
        .setMaintenance4(true);

    featureChain
        .get<vk::PhysicalDeviceShaderObjectFeaturesEXT>() //
        .setShaderObject(true);

    featureChain
        .get<vk::PhysicalDeviceMeshShaderFeaturesEXT>() //
        .setMeshShader(true)
        .setTaskShader(true);

    return featureChain;
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
