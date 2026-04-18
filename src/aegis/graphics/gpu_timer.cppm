module;

#include "graphics/vulkan/vulkan_include.h"

#include <vector>
#include <array>
#include <string>
#include <string_view>

#define AGX_GPU_PROFILE_SCOPE(cmd, name) Aegis::Graphics::GPUScopeTimer gpuTimer##__LINE__(cmd, name)
#define AGX_GPU_PROFILE_FUNCTION(cmd) AGX_GPU_PROFILE_SCOPE(cmd, __FUNCTION__)

export module Aegis.Graphics.GPUTimer;

import Aegis.Graphics.Globals;
import Aegis.Graphics.VulkanContext;

export namespace Aegis::Graphics
{
	struct GPUTimingResult
	{
		std::string name;
		double timeMs{ 0.0 };
	};

	class GPUTimerManager
	{
	public:
		static constexpr uint32_t MAX_QUERY_COUNT = 128;

		GPUTimerManager() :
			m_timestampPeriod{ VulkanContext::device().properties().limits.timestampPeriod }
		{
			VkQueryPoolCreateInfo queryPoolInfo{
				.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
				.queryType = VK_QUERY_TYPE_TIMESTAMP,
				.queryCount = MAX_QUERY_COUNT,
			};

			for (auto& pool : m_queryPools)
			{
				vkCreateQueryPool(VulkanContext::device(), &queryPoolInfo, nullptr, &pool);
			}

			s_instance = this;
		}

		~GPUTimerManager()
		{
			s_instance = nullptr;

			for (auto& pool : m_queryPools)
			{
				vkDestroyQueryPool(VulkanContext::device(), pool, nullptr);
			}
		}

		[[nodiscard]] static auto instance() -> GPUTimerManager&
		{
			AGX_ASSERT_X(s_instance != nullptr, "GPUTimerManager not initialized");
			return *s_instance;
		}

		[[nodiscard]] auto queryPool() const -> VkQueryPool { return m_queryPools[m_frameIndex]; }
		[[nodiscard]] auto timings() const -> const std::vector<GPUTimingResult>& { return m_results; }

		auto aquireQueryIndices(std::string_view name) -> std::pair<uint32_t, uint32_t>
		{
			auto& queryInfo = m_queries[m_frameIndex];
			AGX_ASSERT_X(queryInfo.currentQuery + 2 <= MAX_QUERY_COUNT, "Exceeded maximum GPU query count");
			queryInfo.names.emplace_back(name);
			return { queryInfo.currentQuery++, queryInfo.currentQuery++ };
		}

		void resolveTimings(VkCommandBuffer cmd, uint32_t frameIndex)
		{
			m_frameIndex = frameIndex;

			// Retrieve timestamps for previous frame N
			auto queryCount = m_queries[m_frameIndex].currentQuery;
			if (queryCount > 0)
			{
				std::vector<uint64_t> timestamps(queryCount);
				vkGetQueryPoolResults(VulkanContext::device(), m_queryPools[m_frameIndex], 0, queryCount,
					sizeof(uint64_t) * timestamps.size(), timestamps.data(), sizeof(uint64_t),
					VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

				std::size_t queryCount = m_queries[m_frameIndex].names.size();
				AGX_ASSERT_X(queryCount * 2 == timestamps.size(), "Mismatch between query names and timestamps");

				m_results.clear();
				for (std::size_t i = 0; i < queryCount; ++i)
				{
					uint64_t start = timestamps[i * 2];
					uint64_t end = timestamps[i * 2 + 1];
					double timeNs = static_cast<double>(end - start) * m_timestampPeriod;
					m_results.emplace_back(m_queries[m_frameIndex].names[i], timeNs / 1'000'000.0);
				}
			}

			// Reset for recording this frame N
			m_queries[m_frameIndex].currentQuery = 0;
			vkCmdResetQueryPool(cmd, m_queryPools[m_frameIndex], 0, MAX_QUERY_COUNT);
			m_queries[m_frameIndex].names.clear();
		}

	private:
		static inline GPUTimerManager* s_instance{ nullptr };

		struct QueryInfo
		{
			std::vector<std::string> names;
			uint32_t currentQuery{ 0 };
		};

		std::array<VkQueryPool, MAX_FRAMES_IN_FLIGHT> m_queryPools{};
		std::array<QueryInfo, MAX_FRAMES_IN_FLIGHT> m_queries{};
		double m_timestampPeriod{ 0.0f };
		uint32_t m_frameIndex{ 0 };

		std::vector<GPUTimingResult> m_results{};
	};

	class GPUTimer
	{
	public:
		GPUTimer(VkCommandBuffer cmd)
			: m_cmd(cmd)
		{}

		void start(std::string_view name)
		{
			auto [start, end] = GPUTimerManager::instance().aquireQueryIndices(name);
			m_queryStart = start;
			m_queryEnd = end;
			vkCmdWriteTimestamp(m_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, GPUTimerManager::instance().queryPool(), m_queryStart);
		}

		void end()
		{
			vkCmdWriteTimestamp(m_cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GPUTimerManager::instance().queryPool(), m_queryEnd);
		}

	private:
		VkCommandBuffer m_cmd;
		uint32_t m_queryStart{ 0 };
		uint32_t m_queryEnd{ 0 };
	};

	class GPUScopeTimer
	{
	public:
		GPUScopeTimer(VkCommandBuffer cmd, std::string_view name) :
			m_timer{ cmd }
		{
			m_timer.start(name);
		}

		~GPUScopeTimer()
		{
			m_timer.end();
		}

	private:
		GPUTimer m_timer;
	};
}
