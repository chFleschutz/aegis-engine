module;
#include <cassert>
#include <expected>
#include <ranges>

module aegis.rhi;
import :queue;
import :error;
import :vulkan_conversions;
import vulkan_hpp;

namespace aegis::rhi
{
Queue::Queue(vk::raii::Queue queue, Semaphore semaphore, std::uint32_t queueFamily) :
    m_queue{ std::move(queue) },
    m_timeline{ std::move(semaphore) },
    m_queueFamily{ queueFamily }
{
}

auto Queue::submit(const SubmitInfo& info) -> std::expected<TimelineValue, Error>
{
    m_currentValue += 1;

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
            .value = m_currentValue,
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

    return std::expected<std::uint64_t, Error>{ m_currentValue };
}

auto Queue::submit(const CommandBuffer& cmd) -> std::expected<TimelineValue, Error>
{
    vk::CommandBufferSubmitInfo cmdInfo{
        .commandBuffer = *cmd,
    };

    vk::SubmitInfo2 submitInfo{
        .waitSemaphoreInfoCount = 0,
        .pWaitSemaphoreInfos = nullptr,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdInfo,
        .signalSemaphoreInfoCount = 0,
        .pSignalSemaphoreInfos = nullptr,
    };

    if (auto result = m_queue.submit2(submitInfo); result != vk::Result::eSuccess)
        return makeError(toRHI(result));

    return std::expected<TimelineValue, Error>{ m_currentValue };
}

auto Queue::wait(TimelineValue timePoint) const -> bool
{
    assert(*m_timeline);

    vk::Semaphore waitSem = *m_timeline;
    auto waitInfo = vk::SemaphoreWaitInfo{}
        .setSemaphores(waitSem)
        .setValues(timePoint);

    auto result = m_timeline->getDevice().waitSemaphores(waitInfo,
        std::numeric_limits<std::uint64_t>::max(),
        *m_timeline->getDispatcher());
    return result == vk::Result::eSuccess;
}
}
