module;

#include "graphics/globals.h"

export module Aegis.Graphics.DeletionQueue;

export namespace Aegis::Graphics
{
	/// @brief Manages the deletion of vulkan objects to ensure they are not deleted while in use
	/// @note Deletion is deferred for SwapChain::MAX_FRAMES_IN_FLIGHT frames 
	class DeletionQueue
	{
	public:
		DeletionQueue() = default;
		DeletionQueue(const DeletionQueue&) = delete;
		DeletionQueue(DeletionQueue&&) = delete;
		~DeletionQueue()
		{
			flushAll();
		}

		DeletionQueue& operator=(const DeletionQueue&) = delete;
		DeletionQueue& operator=(DeletionQueue&&) = delete;

		void schedule(std::function<void()>&& function)
		{
			m_pendingDeletions[m_currentFrameIndex].deletors.emplace_back(std::move(function));
		}

		void flush(uint32_t frameIndex)
		{
			m_currentFrameIndex = frameIndex;

			auto& deletors = m_pendingDeletions[frameIndex].deletors;
			for (auto& deleteFunc : deletors)
			{
				if (deleteFunc)
					deleteFunc();
			}
			deletors.clear();
		}

		void flushAll()
		{
			for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
			{
				flush(i);
			}
		}

	private:
		struct DeletorQueue
		{
			std::vector<std::function<void()>> deletors;
		};

		std::array<DeletorQueue, MAX_FRAMES_IN_FLIGHT> m_pendingDeletions;
		uint32_t m_currentFrameIndex{ 0 };
	};
}