module;
#include <expected>
#include <ranges>

module aegis.rhi;
import :queue;
import :error;
import :vulkan;
import vulkan_hpp;

namespace aegis::rhi
{
Queue::Queue(vk::raii::Queue queue, vk::raii::Semaphore semaphore, std::uint32_t queueFamily) :
    m_queue{ std::move(queue) },
    m_timeline{ std::move(semaphore) },
    m_queueFamily{ queueFamily }
{
}

auto Queue::submit(const SubmitInfo& info)
    -> std::expected<std::uint64_t, Error>
{
    m_submitCounter += 1;

    vk::SemaphoreSubmitInfo waitInfo{
        .semaphore = *info.waitSemaphore,
        .value = 0,
        .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    };

    vk::CommandBufferSubmitInfo cmdInfo{
        .commandBuffer = *info.commandBuffer,
    };

    auto signalInfos = std::array{
        vk::SemaphoreSubmitInfo{
            .semaphore = *m_timeline,
            .value = m_submitCounter,
            .stageMask = vk::PipelineStageFlagBits2::eAllGraphics,
        },
        vk::SemaphoreSubmitInfo{
            .semaphore = *info.signalSemaphore,
            .value = 0,
            .stageMask = vk::PipelineStageFlagBits2::eAllGraphics,
        },
    };

    vk::SubmitInfo2 submitInfo{
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &waitInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdInfo,
        .signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size()),
        .pSignalSemaphoreInfos = signalInfos.data(),
    };

    if (auto result = m_queue.submit2(submitInfo); result != vk::Result::eSuccess)
        return makeError(toRHI(result));

    return m_submitCounter;
}

auto Queue::wait(std::uint64_t timePoint) const
    -> bool
{
    auto waitInfo = vk::SemaphoreWaitInfo{}
        .setSemaphores(*m_timeline)
        .setValues(timePoint);

    auto result = m_timeline.getDevice().waitSemaphores(waitInfo,
        std::numeric_limits<std::uint64_t>::max(),
        *m_timeline.getDispatcher());
    return result == vk::Result::eSuccess;
}

auto Queue::waitIdle() const
    -> bool
{
    auto result = m_queue.waitIdle();
    return result == vk::Result::eSuccess;
}
}
